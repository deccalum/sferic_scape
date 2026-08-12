#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace qr {
class Connection;
}

namespace sferic::log {

enum class Level { Debug, Info, Warn, Error };

constexpr std::string_view to_string(Level l) {
  switch (l) {
    case Level::Debug:
      return "debug";
    case Level::Info:
      return "info";
    case Level::Warn:
      return "warn";
    case Level::Error:
      return "error";
  }
  std::unreachable();
}

inline Level from_string(std::string_view s, std::type_identity<Level>) {
  if (s == "debug") return Level::Debug;
  if (s == "info") return Level::Info;
  if (s == "warn") return Level::Warn;
  if (s == "error") return Level::Error;
  throw std::invalid_argument("unknown log::Level: " + std::string(s));
}

struct LogConfig {
  size_t flush_rows = 256;  // buffered rows before a batched INSERT is issued
  bool echo_stdout = true;  // mirror to stdout/stderr for live feedback
};

void init(qr::Connection& db, const LogConfig& config);
void shutdown();  // Flush the buffer and detach. Must be called before `db` is destroyed.
void set_echo_stdout(bool on);
bool echo_stdout();
void write(Level level, std::string_view func, std::string_view msg);
void write_mem(Level level, std::string_view func, std::string_view msg, size_t bytes);
void progress(std::string_view func, size_t current, size_t total, double audio_ms,
              size_t model_bytes, size_t stdout_interval = 100);
std::string fmt_bytes(size_t bytes);  // Format bytes as human-readable: B / KB / MB / GB

// RAII scope timer — logs entry on construction, exit + elapsed on destruction.
struct ScopeTimer {
  explicit ScopeTimer(std::string_view func, std::string_view note = "");
  ~ScopeTimer();

 private:
  std::string func_;
  std::string note_;
  long long start_us_;
};

}  // namespace sferic::log

#define SFERIC_LOG(lvl, msg) ::sferic::log::write(::sferic::log::Level::lvl, __func__, (msg))

#define SFERIC_LOG_MEM(lvl, msg, bytes) \
  ::sferic::log::write_mem(::sferic::log::Level::lvl, __func__, (msg), (bytes))

#define SFERIC_SCOPE(note) ::sferic::log::ScopeTimer _sferic_scope_(__func__, (note))

#define SFERIC_PROGRESS(cur, tot, audio_ms, bytes) \
  ::sferic::log::progress(__func__, (cur), (tot), (audio_ms), (bytes))

#define SFERIC_PROGRESS_EVERY(cur, tot, audio_ms, bytes, every) \
  ::sferic::log::progress(__func__, (cur), (tot), (audio_ms), (bytes), (every))
