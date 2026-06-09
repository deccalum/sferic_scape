#pragma once

#include <filesystem>
#include <vector>

#include "cli/commands.h"

namespace sferic::cli {

// Opens an interactive TUI browser at root. Returns the paths the user confirmed
// (files and/or directories). Empty vector means the user cancelled. Output toggles
// (e.g. write_json) are edited live via in-browser keys and written back to `opts`.
std::vector<std::filesystem::path> browse(const std::filesystem::path& root, AnalyzeOptions& opts);

}  // namespace sferic::cli
