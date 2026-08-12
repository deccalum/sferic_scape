#include "ui/text.h"

#include <iomanip>
#include <sstream>

namespace sferic::ui {
namespace {

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

size_t offset_of(std::string_view s, size_t chars) {
  size_t i = 0;
  for (size_t n = 0; i < s.size() && n < chars; ++n) {
    ++i;
    while (i < s.size() && is_continuation(s[i])) ++i;
  }
  return i;
}

}  // namespace

size_t display_width(std::string_view s) {
  size_t n = 0;
  for (const char c : s) n += is_continuation(c) ? 0 : 1;
  return n;
}

std::string trunc(std::string_view s, size_t max) {
  if (display_width(s) <= max) return std::string(s);
  if (max <= 1) return std::string(s.substr(0, offset_of(s, max)));
  return std::string(s.substr(0, offset_of(s, max - 1))) + "…";
}

std::string pad(std::string_view s, size_t width) {
  const size_t w = display_width(s);
  return std::string(s) + std::string(w < width ? width - w : 0, ' ');
}

std::string fmt_len(double seconds) {
  if (seconds <= 0.0) return "-";
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(seconds < 10.0 ? 2 : 1) << seconds << "s";
  return ss.str();
}

}  // namespace sferic::ui
