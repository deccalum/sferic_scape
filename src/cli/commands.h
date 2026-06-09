#pragma once

#include <filesystem>

namespace sferic::cli {

struct AnalyzeOptions {
  bool emit_unhealed = false; // for process entries, also write the un-healed (.raw) outputs
  bool write_json = false;    // persist the ParametricModel as <stem>jsonmodel.json — off by default
  bool playback = false;      // TODO
  bool plot = false;          // TODO
};

// Analyze a single WAV file or every WAV under a directory tree.
void analyze(const std::filesystem::path& input, const AnalyzeOptions& opts);

}  // namespace sferic::cli
