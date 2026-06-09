#include "core/logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace sferic {
namespace log {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
bool g_initialised = false;

std::string timestamp_now() {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto now_t = system_clock::to_time_t(now);
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  std::tm tm_buf{};
  localtime_r(&now_t, &tm_buf);

  std::ostringstream ss;
  ss << std::put_time(&tm_buf, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
  return ss.str();
}

std::string timestamp_file_suffix() {
  using namespace std::chrono;
  auto now_t = system_clock::to_time_t(system_clock::now());
  std::tm tm_buf{};
  localtime_r(&now_t, &tm_buf);

  std::ostringstream ss;
  ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
  return ss.str();
}

const char* level_str(Level l) {
  switch (l) {
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO ";
    case Level::Warn:
      return "WARN ";
    case Level::Error:
      return "ERROR";
  }
  return "?    ";
}

void emit(Level level, std::string_view func, std::string_view msg) {
  const std::string ts = timestamp_now();
  const char* lvl = level_str(level);

  // File — always
  if (g_file.is_open()) {
    g_file << '[' << ts << "] [" << lvl << "] " << func << ": " << msg << '\n';
    g_file.flush();
  }

  // Stdout — Debug suppressed; Info/Warn/Error always
  if (level >= Level::Info) {
    std::fprintf(level >= Level::Warn ? stderr : stdout, "[%s] [%s] %s: %s\n", ts.c_str(), lvl, func.data(), msg.data());
  }
}

}  // namespace

void init(std::string_view path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialised) return;

  // If path ends with '/' treat it as a directory; generate filename inside it.
  std::string file_path;
  if (path.empty() || (!path.empty() && path.back() == '/')) {
    const std::string dir = path.empty() ? "." : std::string(path);
    std::filesystem::create_directories(dir);
    file_path = dir + "sferic_" + timestamp_file_suffix() + ".log";
  } else {
    file_path = std::string(path);
  }

  g_file.open(file_path, std::ios::out | std::ios::app);
  g_initialised = true;

  const std::string msg = "log opened — " + file_path;
  emit(Level::Info, "sferic::log::init", msg);
}

void write(Level level, std::string_view func, std::string_view msg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  emit(level, func, msg);
}

void write_mem(Level level, std::string_view func, std::string_view msg, size_t bytes) {
  std::lock_guard<std::mutex> lock(g_mutex);
  emit(level, func, std::string(msg) + " — " + fmt_bytes(bytes));
}

void progress(std::string_view func, size_t current, size_t total, double audio_ms,
              size_t model_bytes, size_t stdout_interval) {
  std::lock_guard<std::mutex> lock(g_mutex);

  std::ostringstream ss;
  ss << "ms " << std::setw(6) << current << " / " << std::setw(6) << total << " | audio "
     << std::setw(8) << std::fixed << std::setprecision(1) << audio_ms << " ms"
     << " | model " << fmt_bytes(model_bytes);

  const std::string line = ss.str();

  // Always write to file
  if (g_file.is_open()) {
    g_file << '[' << timestamp_now() << "] [INFO ] " << func << ": " << line << '\n';
    // Flush only every 100 lines to avoid hammering disk
    if (current % 100 == 0) g_file.flush();
  }

  // Write to stdout at the requested interval or on the final entry. The line is
  // rewritten in place (\r + clear-to-EOL) so a loop occupies one scrolling line;
  // the final entry terminates with a newline so subsequent logs start clean.
  if (current % stdout_interval == 0 || current + 1 >= total) {
    const bool last = current + 1 >= total;
    std::printf("\r\033[K[%s] %.*s: %s%s", timestamp_now().c_str(),
                static_cast<int>(func.size()), func.data(), line.c_str(), last ? "\n" : "");
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
  start_us_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string msg = note_.empty() ? "→ enter" : ("→ enter  " + note_);
  emit(Level::Info, func_, msg);
}

ScopeTimer::~ScopeTimer() {
  const long long end_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  const double elapsed_ms = static_cast<double>(end_us - start_us_) / 1000.0;
  std::lock_guard<std::mutex> lock(g_mutex);
  std::ostringstream ss;
  ss << "← exit   " << (note_.empty() ? "" : (note_ + "  ")) << std::fixed << std::setprecision(3)
     << elapsed_ms << " ms";
  emit(Level::Info, func_, ss.str());
}

}  // namespace log
}  // namespace sferic
