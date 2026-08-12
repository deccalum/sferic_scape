#pragma once

#include <cstddef>
#include <vector>

namespace sferic {

using Sample = float;
using SampleBuffer = std::vector<Sample>;

// SpectralFrame — one windowed FFT output, produced by STFT::analyze().
struct SpectralFrame {
  double time_seconds;             // frame centre time in the source
  std::vector<double> magnitudes;  // |X[k]| per bin
  std::vector<double> phases;      // arg(X[k]) per bin
  size_t fft_size;                 // FFT length used for this frame
  double sample_rate;              // Hz

  double bin_frequency(size_t bin) const {
    return static_cast<double>(bin) * sample_rate / static_cast<double>(fft_size);
  }
  size_t num_bins() const { return magnitudes.size(); }
};

}  // namespace sferic
