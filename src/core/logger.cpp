#include "core/logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <iomanip>
#include <mutex>
#include <qr/qr.hpp>
#include <sstream>
#include <vector>

#include "io/schema.h"

namespace sferic::log {
namespace {

struct Row {
  std::string level;                             // log::Level as string
  std::string func;                              // __func__ of the call site
  std::string message;                           // body
  std::chrono::system_clock::time_point at;      // client stamp (not server_default)
};

std::mutex g_mutex;
qr::Connection* g_db = nullptr;
LogConfig g_config;
std::string g_run;
std::vector<Row> g_buffer;

std::string timestamp_now() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto now_t = system_clock::to_time_t(now);
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  std::tm tm_buf{};
  localtime_r(&now_t, &tm_buf);

  std::ostringstream ss;
  ss << std::put_time(&tm_buf, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
     << ms.count();
  return ss.str();
}

std::string run_tag() {
  const auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm_buf{};
  localtime_r(&now_t, &tm_buf);

  std::ostringstream ss;
  ss << std::put_time(&tm_buf, "sferic_%Y%m%d_%H%M%S");
  return ss.str();
}

void flush_locked() {
  if (g_db == nullptr || g_buffer.empty()) return;
  using Table = ::sferic::io::schema::Log;
  auto query = qr::bulk_insert_into<Table>(Table::run, Table::level, Table::func, Table::message,
                                           Table::logged_at);
  for (const Row& r : g_buffer) query.add(g_run, r.level, r.func, r.message, r.at);
  g_buffer.clear();
  try {
    g_db->insert_bulk(std::move(query));
  } catch (const std::exception& e) {
    g_db = nullptr;
    std::fprintf(stderr, "[log] database sink detached — insert failed: %s\n", e.what());
  }
}

void buffer_locked(Level level, std::string_view func, std::string_view msg) {
  if (g_db == nullptr) return;
  g_buffer.push_back(Row{std::string(to_string(level)), std::string(func), std::string(msg),
                         std::chrono::system_clock::now()});
  if (level >= Level::Error || g_buffer.size() >= g_config.flush_rows) flush_locked();
}

void echo_locked(Level level, std::string_view func, std::string_view msg) {
  if (!g_config.echo_stdout || level < Level::Info) return;
  std::fprintf(level >= Level::Warn ? stderr : stdout, "[%s] [%-5s] %.*s: %.*s\n",
               timestamp_now().c_str(), std::string(to_string(level)).c_str(),
               static_cast<int>(func.size()), func.data(), static_cast<int>(msg.size()),
               msg.data());
}

void emit_locked(Level level, std::string_view func, std::string_view msg) {
  buffer_locked(level, func, msg);
  echo_locked(level, func, msg);
}

}  // namespace

void init(qr::Connection& db, const LogConfig& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_db != nullptr) return;

  g_db = &db;
  g_config = config;
  g_run = run_tag();
  g_buffer.reserve(config.flush_rows);

  emit_locked(Level::Info, "sferic::log::init", "log run " + g_run);
}

void shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_db == nullptr) return;

  emit_locked(Level::Info, "sferic::log::shutdown", "closing log run " + g_run);
  flush_locked();
  g_db = nullptr;
}

void set_echo_stdout(bool on) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config.echo_stdout = on;
}

bool echo_stdout() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config.echo_stdout;
}

void write(Level level, std::string_view func, std::string_view msg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  emit_locked(level, func, msg);
}

void write_mem(Level level, std::string_view func, std::string_view msg, size_t bytes) {
  std::lock_guard<std::mutex> lock(g_mutex);
  emit_locked(level, func, std::string(msg) + " — " + fmt_bytes(bytes));
}

void progress(std::string_view func, size_t current, size_t total, double audio_ms,
              size_t model_bytes, size_t stdout_interval) {
  std::lock_guard<std::mutex> lock(g_mutex);

  const bool last = current >= total;
  if ((current - 1) % stdout_interval != 0 && !last) return;

  std::ostringstream ss;
  ss << "ms " << std::setw(6) << current << " / " << std::setw(6) << total << " | audio "
     << std::setw(8) << std::fixed << std::setprecision(1) << audio_ms << " ms"
     << " | model " << fmt_bytes(model_bytes);
  const std::string line = ss.str();

  buffer_locked(Level::Info, func, line);
  if (g_config.echo_stdout) {
    std::printf("\r\033[K[%s] %.*s: %s%s", timestamp_now().c_str(), static_cast<int>(func.size()),
                func.data(), line.c_str(), last ? "\n" : "");
    std::fflush(stdout);
  }
}

std::string fmt_bytes(size_t bytes) {
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(1);
  if (bytes >= 1024ULL * 1024 * 1024)
    ss << static_cast<double>(bytes) / (1024.0 * 1024 * 1024) << " GB";
  else if (bytes >= 1024ULL * 1024)
    ss << static_cast<double>(bytes) / (1024.0 * 1024) << " MB";
  else if (bytes >= 1024ULL)
    ss << static_cast<double>(bytes) / 1024.0 << " KB";
  else
    ss << bytes << " B";
  return ss.str();
}

ScopeTimer::ScopeTimer(std::string_view func, std::string_view note) : func_(func), note_(note) {
  start_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();
  std::lock_guard<std::mutex> lock(g_mutex);
  emit_locked(Level::Info, func_, note_.empty() ? "-> enter" : ("-> enter  " + note_));
}

ScopeTimer::~ScopeTimer() {
  const long long end_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
  const double elapsed_ms = static_cast<double>(end_us - start_us_) / 1000.0;
  std::lock_guard<std::mutex> lock(g_mutex);
  std::ostringstream ss;
  ss << "← exit   " << (note_.empty() ? "" : (note_ + "  ")) << std::fixed << std::setprecision(3)
     << elapsed_ms << " ms";
  emit_locked(Level::Info, func_, ss.str());
}

}  // namespace sferic::log
