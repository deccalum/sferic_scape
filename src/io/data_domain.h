#pragma once

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace sferic {
namespace io {

enum class Segment : uint8_t { Full, Attack, Body, Tail };
enum class FrequencyBias : uint8_t { Sub, Mid, High };

struct ThunderData {
  std::vector<Segment> seg_type;
  std::vector<FrequencyBias> freq_bias;
  int sub_strikes = 0;
  bool pre_crack = false;
  bool stereo_fx = false;
};

struct RainData {
  bool loopable = false;
};

struct WindData {
  std::string intensity;
};

struct OceanData {};

using DomainData = std::variant<std::monostate, ThunderData, RainData, WindData, OceanData>;

struct LibraryEntry {
  std::filesystem::path wav_path;
  std::string recording_id;
  std::string segment_id;
  int sample_rate_khz = 0;
  int rating = 0;
  bool process = false;
  double segment_start_s = 0.0;
  std::string source;
  std::string notes;
  DomainData payload;
};

// Parse a segment id to its start offset in seconds. Accepts "SSSs_MMMms"
// (e.g. "095s_759ms"), with or without a trailing ".wav".
double parse_segment_seconds(std::string segment_id);

std::vector<LibraryEntry> load_library(const std::filesystem::path& root);

}  // namespace io
}  // namespace sferic
