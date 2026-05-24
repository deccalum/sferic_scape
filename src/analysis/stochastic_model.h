#pragma once

#include <cstddef>
#include <vector>

#include "core/buffer.h"

namespace sferic {
namespace analysis {

// A single frame of the stochastic (noise) model: the spectral envelope
// of the residual signal at one point in time.
struct StochasticFrame {
  double time_seconds;
  // Spectral envelope: magnitude values at evenly-spaced frequency points.
  // num_points values covering 0 Hz to Nyquist.
  std::vector<double> envelope;
  double frequency_spacing;  // Hz between envelope points
};

// Complete stochastic model: a time series of spectral envelopes
// describing how the noise character evolves over the sound's duration.
struct StochasticModel {
  double sample_rate;
  size_t num_envelope_points;  // Spectral resolution of each envelope
  std::vector<StochasticFrame> frames;

  bool empty() const {
    return frames.empty();
  }
  double duration() const {
    if (frames.empty()) return 0.0;
    return frames.back().time_seconds - frames.front().time_seconds;
  }
};

struct StochasticModelConfig {
  // Analysis window size for the residual STFT
  size_t fft_size = 2048;
  size_t hop_size = 512;

  // Number of points in the spectral envelope (downsampled from full spectrum).
  // Lower = more compact, higher = more accurate noise texture.
  size_t num_envelope_points = 128;

  // LPC order for spectral envelope estimation.
  // Higher = more detailed envelope, 0 = use direct magnitude smoothing instead.
  size_t lpc_order = 0;  // Default: use magnitude smoothing (simpler, no Eigen dependency)

  // Smoothing window size (in bins) for direct magnitude envelope.
  // Only used when lpc_order == 0.
  size_t smoothing_bins = 15;
};

class StochasticAnalyzer {
 public:
  explicit StochasticAnalyzer(const StochasticModelConfig& config, double sample_rate);

  // Analyze the residual signal and produce a stochastic model.
  // Input must be mono.
  StochasticModel analyze(const AudioBuffer& residual) const;

 private:
  StochasticModelConfig config_;
  double sample_rate_;

  // Smooth a magnitude spectrum into a spectral envelope
  std::vector<double> smooth_envelope(const std::vector<double>& magnitudes) const;

  // Downsample a full-resolution envelope to num_envelope_points
  std::vector<double> downsample_envelope(const std::vector<double>& full_envelope) const;
};

// Synthesize noise from a stochastic model.
// Generates filtered noise that matches the time-varying spectral envelope.
AudioBuffer synthesize_stochastic(const StochasticModel& model, size_t num_frames,
                                  double sample_rate);

}  // namespace analysis
}  // namespace sferic
