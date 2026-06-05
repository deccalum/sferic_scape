#pragma once

#include "core/types.h"

namespace sferic {

class AudioBuffer {
 public:
  AudioBuffer() = default;
  AudioBuffer(size_t num_channels, size_t num_frames, double sample_rate);

  // Access
  size_t num_channels() const { return num_channels_; }
  size_t num_frames() const { return num_frames_; }
  double sample_rate() const { return sample_rate_; }
  double duration() const;

  // Per-channel access
  Sample* channel(size_t ch);
  const Sample* channel(size_t ch) const;

  // Single sample access
  Sample& at(size_t ch, size_t frame);
  const Sample& at(size_t ch, size_t frame) const;

  // Analysis
  Sample peak_amplitude() const;
  double rms() const;

  // Utilities
  bool empty() const { return data_.empty(); }
  void resize(size_t num_channels, size_t num_frames, double sample_rate);
  void clear();

  // Mix down to mono (averages channels)
  AudioBuffer to_mono() const;

 private:
  SampleBuffer data_;  // Interleaved: [ch0_f0, ch1_f0, ch0_f1, ch1_f1, ...]
  size_t num_channels_ = 0;
  size_t num_frames_ = 0;
  double sample_rate_ = 0.0;
};

}  // namespace sferic
