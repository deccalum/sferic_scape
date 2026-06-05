#pragma once

#include <complex>
#include <cstddef>
#include <random>
#include <vector>

#include "analysis/spectral_envelope.h"

namespace sferic {
namespace synthesis {

// Generates noise shaped to a time-varying spectral envelope via overlap-add
// random-phase spectrum synthesis — no oscillators, no time-domain noise FFT.
class NoiseGenerator {
 public:
  NoiseGenerator();

  void load_model(const analysis::SpectralEnvelope& model);
  void clear();

  // Render a block of shaped noise starting at time t into output[0..num_samples).
  size_t render(float* output, size_t num_samples, double start_time);

  bool has_model() const { return !model_.ms.empty(); }

 private:
  analysis::SpectralEnvelope model_;
  std::mt19937_64 rng_;

  // Derived in load_model().
  size_t fft_size_ = 0;
  size_t hop_size_ = 0;
  double ola_norm_ = 0.0;  // hop / sum(w²) × N — power-normalised OLA gain

  std::vector<double> window_;  // cached synthesis Hann window
  std::vector<float> overlap_buffer_;
  size_t synthesis_pos_ = 0;

  std::vector<double> get_envelope_at_time(double t) const;
  void synthesize_grain(double center_time, std::vector<float>& output);
  void irfft(const std::vector<std::complex<double>>& input, std::vector<float>& output);
};

}  // namespace synthesis
}  // namespace sferic
