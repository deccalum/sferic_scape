#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/buffer.h"

namespace sferic::io {

struct AudioFileInfo {
  size_t num_channels;  // channel count
  size_t num_frames;    // frames per channel
  double sample_rate;   // Hz
  double duration;      // seconds (= num_frames / sample_rate)
  std::string format;   // libsndfile format label
};

AudioBuffer load(const std::string& path);
AudioFileInfo info(const std::string& path);
AudioBuffer decode(std::span<const std::byte> bytes);
std::vector<std::byte> encode_flac(const AudioBuffer& buffer);
void save(const AudioBuffer& buffer, const std::string& path);

}  // namespace sferic::io
