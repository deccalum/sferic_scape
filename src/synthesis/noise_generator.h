#pragma once

#include <cstddef>
#include <complex>
#include <random>
#include <vector>

#include "analysis/stochastic_model.h"

namespace sferic {
namespace synthesis {

// Generates filtered white noise matching a time-varying spectral envelope.
// Uses overlap-add synthesis with IFFT to shape noise in the frequency domain.
class NoiseGenerator {
 public:
  explicit NoiseGenerator(double sample_rate);

  // Load a stochastic model
  void load_model(const analysis::StochasticModel& model);

  // Clear the loaded model
  void clear();

  // Render a block of filtered noise samples starting at time t
  // Returns the number of samples actually rendered
  size_t render(float* output, size_t num_samples, double start_time);

  // Check if a model is loaded
  bool has_model() const {
    return !model_.frames.empty();
  }

 private:
  double sample_rate_;
  analysis::StochasticModel model_;
  std::mt19937 rng_;
  std::normal_distribution<float> dist_;

  // FFT/IFFT parameters
  static constexpr size_t fft_size_ = 2048;
  static constexpr size_t hop_size_ = 512;

  // Overlap-add state
  std::vector<float> overlap_buffer_;
  size_t synthesis_pos_ = 0;

  // Get the spectral envelope at time t via linear interpolation
  std::vector<double> get_envelope_at_time(double t) const;

  // Generate a block of white noise
  void generate_white_noise(float* buffer, size_t num_samples);

  // Synthesize one grain of filtered noise using frequency-domain shaping
  void synthesize_grain(double center_time, std::vector<float>& output);

  // FFT/IFFT helpers (using naive DFT for simplicity, can swap for FFTW3)
  void rfft(const std::vector<float>& input, std::vector<std::complex<double>>& output);
  void irfft(const std::vector<std::complex<double>>& input, std::vector<float>& output);
};

}  // namespace synthesis
}  // namespace sferic
