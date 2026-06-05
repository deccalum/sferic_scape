#pragma once

#include <string>

#include "core/buffer.h"

namespace sferic {
namespace io {

struct AudioFileInfo {
  size_t num_channels;
  size_t num_frames;
  double sample_rate;
  double duration;
  std::string format;  // e.g. "WAV", "FLAC", "AIFF"
};

// Load an audio file into an AudioBuffer.
// Supports WAV, FLAC, AIFF, OGG via libsndfile.
AudioBuffer load(const std::string& path);

// Get file info without loading all sample data.
AudioFileInfo info(const std::string& path);

// Save an AudioBuffer to an audio file.
// Format is inferred from the file extension (.wav, .flac, .aiff).
void save(const AudioBuffer& buffer, const std::string& path);

}  // namespace io
}  // namespace sferic
