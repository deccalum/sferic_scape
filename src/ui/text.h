#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sferic::ui {

size_t display_width(std::string_view s);
std::string trunc(std::string_view s, size_t max);
std::string pad(std::string_view s, size_t width);
std::string fmt_len(double seconds);

}  // namespace sferic::ui
