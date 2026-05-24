#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sferic {

using Sample = float;
using SampleBuffer = std::vector<Sample>;

struct SpectralPeak {
  double frequency;  // Hz (refined by parabolic interpolation)
  double amplitude;  // Linear scale
  double phase;      // Radians
};

struct SpectralFrame {
  double time_seconds;
  std::vector<double> magnitudes;
  std::vector<double> phases;
  size_t fft_size;
  double sample_rate;

  double bin_frequency(size_t bin) const {
    return static_cast<double>(bin) * sample_rate / static_cast<double>(fft_size);
  }

  size_t num_bins() const {
    return magnitudes.size();
  }
};

struct PartialTrack {
  uint32_t id;
  double birth_time;
  double death_time;
  std::vector<double> times;
  std::vector<double> frequencies;
  std::vector<double> amplitudes;
  std::vector<double> phases;

  size_t num_frames() const {
    return times.size();
  }
  double duration() const {
    return death_time - birth_time;
  }
};

}  // namespace sferic
