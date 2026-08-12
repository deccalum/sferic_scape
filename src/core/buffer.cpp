#include "core/buffer.h"

#include <algorithm>
#include <cmath>

namespace sferic {

AudioBuffer::AudioBuffer(size_t num_channels, size_t num_frames, double sample_rate)
    : data_(num_channels * num_frames, 0.0f),
      num_channels_(num_channels),
      num_frames_(num_frames),
      sample_rate_(sample_rate) {}

double AudioBuffer::duration() const { return static_cast<double>(num_frames_) / sample_rate_; }
Sample* AudioBuffer::data() { return data_.data(); }
const Sample* AudioBuffer::data() const { return data_.data(); }
Sample& AudioBuffer::at(size_t ch, size_t frame) { return data_[frame * num_channels_ + ch]; }
const Sample& AudioBuffer::at(size_t ch, size_t frame) const { return data_[frame * num_channels_ + ch]; }

Sample AudioBuffer::peak_amplitude() const {
  Sample peak = 0.0f;
  for (const auto& s : data_) peak = std::max(peak, std::abs(s));
  return peak;
}

double AudioBuffer::rms() const {
  double sum_sq = 0.0;
  for (const auto& s : data_) sum_sq += static_cast<double>(s) * static_cast<double>(s);
  return std::sqrt(sum_sq / static_cast<double>(data_.size()));
}

void AudioBuffer::resize(size_t num_channels, size_t num_frames, double sample_rate) {
  num_channels_ = num_channels;
  num_frames_ = num_frames;
  sample_rate_ = sample_rate;
  data_.resize(num_channels * num_frames, 0.0f);
}

void AudioBuffer::clear() {
  data_.clear();
  num_channels_ = 0;
  num_frames_ = 0;
  sample_rate_ = 0.0;
}

AudioBuffer AudioBuffer::to_mono() const {
  if (num_channels_ == 1) return *this;
  AudioBuffer mono(1, num_frames_, sample_rate_);
  for (size_t f = 0; f < num_frames_; ++f) {
    double sum = 0.0;
    for (size_t ch = 0; ch < num_channels_; ++ch) sum += static_cast<double>(at(ch, f));
    mono.at(0, f) = static_cast<Sample>(sum / static_cast<double>(num_channels_));
  }
  return mono;
}

}  // namespace sferic
