#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sferic {
namespace log {

enum class Level { Debug, Info, Warn, Error };

// Open the log file. Auto-names it sferic_YYYYMMDD_HHMMSS.log if path is empty.
// Safe to call multiple times — only the first call takes effect.
void init(std::string_view path = "data/logs/");

// Write one log line to file (always) and stdout (Warn/Error always; Info/Debug when verbose).
void write(Level level, std::string_view func, std::string_view msg);

// Same as write(), but appends a formatted memory size to the message.
void write_mem(Level level, std::string_view func, std::string_view msg, size_t bytes);

// Write a progress line to file every call, to stdout every stdout_interval calls.
// current / total are counts; audio_ms is the corresponding audio position.
void progress(std::string_view func, size_t current, size_t total, double audio_ms,
              size_t model_bytes, size_t stdout_interval = 100);

// Format bytes as human-readable: B / KB / MB / GB.
std::string fmt_bytes(size_t bytes);

// RAII scope timer — logs entry on construction, exit + elapsed on destruction.
struct ScopeTimer {
  explicit ScopeTimer(std::string_view func, std::string_view note = "");
  ~ScopeTimer();

 private:
  std::string func_;
  std::string note_;
  long long start_us_;
};

}  // namespace log
}  // namespace sferic

#define SFERIC_LOG(lvl, msg) ::sferic::log::write(::sferic::log::Level::lvl, __func__, (msg))

#define SFERIC_LOG_MEM(lvl, msg, bytes) \
  ::sferic::log::write_mem(::sferic::log::Level::lvl, __func__, (msg), (bytes))

#define SFERIC_SCOPE(note) ::sferic::log::ScopeTimer _sferic_scope_(__func__, (note))

#define SFERIC_PROGRESS(cur, tot, audio_ms, bytes) \
  ::sferic::log::progress(__func__, (cur), (tot), (audio_ms), (bytes))
