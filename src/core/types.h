#pragma once

#include <cstddef>
#include <vector>

namespace sferic {

using Sample = float;
using SampleBuffer = std::vector<Sample>;

// SpectralFrame — one windowed FFT output, produced by STFT::analyze().
struct SpectralFrame {
  double time_seconds;
  std::vector<double> magnitudes;
  std::vector<double> phases;
  size_t fft_size;
  double sample_rate;

  double bin_frequency(size_t bin) const {
    return static_cast<double>(bin) * sample_rate / static_cast<double>(fft_size);
  }
  size_t num_bins() const { return magnitudes.size(); }
};

}  // namespace sferic
