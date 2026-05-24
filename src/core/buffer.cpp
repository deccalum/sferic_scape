#include "core/buffer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sferic {

AudioBuffer::AudioBuffer(size_t num_channels, size_t num_frames, double sample_rate)
    : data_(num_channels * num_frames, 0.0f),
      num_channels_(num_channels),
      num_frames_(num_frames),
      sample_rate_(sample_rate) {}

double AudioBuffer::duration() const {
  if (sample_rate_ <= 0.0) return 0.0;
  return static_cast<double>(num_frames_) / sample_rate_;
}

Sample* AudioBuffer::channel(size_t ch) {
  if (ch >= num_channels_) throw std::out_of_range("Channel index out of range");
  // Data is interleaved, so we can't return a contiguous channel pointer directly.
  // For interleaved data, use at() instead. This returns pointer to first sample of channel
  // in the interleaved layout.
  return data_.data() + ch;
}

const Sample* AudioBuffer::channel(size_t ch) const {
  if (ch >= num_channels_) throw std::out_of_range("Channel index out of range");
  return data_.data() + ch;
}

Sample& AudioBuffer::at(size_t ch, size_t frame) {
  if (ch >= num_channels_ || frame >= num_frames_)
    throw std::out_of_range("Buffer index out of range");
  return data_[frame * num_channels_ + ch];
}

const Sample& AudioBuffer::at(size_t ch, size_t frame) const {
  if (ch >= num_channels_ || frame >= num_frames_)
    throw std::out_of_range("Buffer index out of range");
  return data_[frame * num_channels_ + ch];
}

Sample AudioBuffer::peak_amplitude() const {
  Sample peak = 0.0f;
  for (const auto& s : data_) {
    peak = std::max(peak, std::abs(s));
  }
  return peak;
}

double AudioBuffer::rms() const {
  if (data_.empty()) return 0.0;
  double sum_sq = 0.0;
  for (const auto& s : data_) {
    sum_sq += static_cast<double>(s) * static_cast<double>(s);
  }
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
  if (empty()) return {};

  AudioBuffer mono(1, num_frames_, sample_rate_);
  for (size_t f = 0; f < num_frames_; ++f) {
    double sum = 0.0;
    for (size_t ch = 0; ch < num_channels_; ++ch) {
      sum += static_cast<double>(at(ch, f));
    }
    mono.at(0, f) = static_cast<Sample>(sum / static_cast<double>(num_channels_));
  }
  return mono;
}

}  // namespace sferic
