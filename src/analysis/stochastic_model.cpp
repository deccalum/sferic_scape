#include "analysis/stochastic_model.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

#include "analysis/stft.h"
#include "analysis/window.h"
#include "core/constants.h"

namespace sferic {
namespace analysis {

StochasticAnalyzer::StochasticAnalyzer(const StochasticModelConfig& config, double sample_rate)
    : config_(config), sample_rate_(sample_rate) {}

std::vector<double> StochasticAnalyzer::smooth_envelope(
    const std::vector<double>& magnitudes) const {
  // Moving average smoothing of the magnitude spectrum.
  // This approximates the spectral envelope by removing fine detail (partials).
  size_t n = magnitudes.size();
  std::vector<double> smoothed(n);
  int half = static_cast<int>(config_.smoothing_bins / 2);

  for (size_t i = 0; i < n; ++i) {
    double sum = 0.0;
    int count = 0;
    int lo = std::max(0, static_cast<int>(i) - half);
    int hi = std::min(static_cast<int>(n) - 1, static_cast<int>(i) + half);
    for (int j = lo; j <= hi; ++j) {
      sum += magnitudes[static_cast<size_t>(j)];
      count++;
    }
    smoothed[i] = (count > 0) ? sum / static_cast<double>(count) : 0.0;
  }

  return smoothed;
}

std::vector<double> StochasticAnalyzer::downsample_envelope(
    const std::vector<double>& full_envelope) const {
  size_t target = config_.num_envelope_points;
  if (full_envelope.size() <= target) return full_envelope;

  std::vector<double> downsampled(target);
  double ratio = static_cast<double>(full_envelope.size() - 1) / static_cast<double>(target - 1);

  for (size_t i = 0; i < target; ++i) {
    double pos = static_cast<double>(i) * ratio;
    size_t lo = static_cast<size_t>(pos);
    size_t hi = std::min(lo + 1, full_envelope.size() - 1);
    double frac = pos - static_cast<double>(lo);
    downsampled[i] = full_envelope[lo] + frac * (full_envelope[hi] - full_envelope[lo]);
  }

  return downsampled;
}

StochasticModel StochasticAnalyzer::analyze(const AudioBuffer& residual) const {
  if (residual.num_channels() != 1)
    throw std::invalid_argument("StochasticAnalyzer::analyze requires mono input");

  StochasticModel model;
  model.sample_rate = sample_rate_;
  model.num_envelope_points = config_.num_envelope_points;

  if (residual.num_frames() < config_.fft_size) return model;

  // Run STFT on the residual
  STFTConfig stft_cfg;
  stft_cfg.fft_size = config_.fft_size;
  stft_cfg.hop_size = config_.hop_size;
  stft_cfg.window_type = WindowType::HANN;

  STFT stft(stft_cfg, sample_rate_);
  auto frames = stft.analyze(residual);

  double freq_spacing = (sample_rate_ / 2.0) / static_cast<double>(config_.num_envelope_points - 1);

  for (const auto& frame : frames) {
    // Smooth the magnitude spectrum to get spectral envelope
    auto smoothed = smooth_envelope(frame.magnitudes);

    // Downsample to target resolution
    auto envelope = downsample_envelope(smoothed);

    StochasticFrame sf;
    sf.time_seconds = frame.time_seconds;
    sf.envelope = std::move(envelope);
    sf.frequency_spacing = freq_spacing;

    model.frames.push_back(std::move(sf));
  }

  return model;
}

AudioBuffer synthesize_stochastic(const StochasticModel& model, size_t num_frames,
                                  double sample_rate) {
  AudioBuffer output(1, num_frames, sample_rate);

  if (model.empty()) return output;

  // Use overlap-add synthesis with shaped noise.
  // For each frame of the stochastic model, generate white noise,
  // shape it in the frequency domain, and overlap-add.

  // Determine synthesis FFT size from envelope resolution
  // Use a power-of-2 size that gives at least num_envelope_points bins
  size_t synth_fft = 1;
  while (synth_fft / 2 + 1 < model.num_envelope_points) synth_fft *= 2;

  size_t synth_bins = synth_fft / 2 + 1;
  size_t synth_hop = synth_fft / 4;  // 75% overlap

  auto window = make_window(WindowType::HANN, synth_fft);

  // Normalization for overlap-add with Hann window at 75% overlap
  double window_sum = 0.0;
  for (double w : window) window_sum += w * w;
  double norm = static_cast<double>(synth_hop) / window_sum * static_cast<double>(synth_fft);

  std::mt19937 rng(42);  // Deterministic seed for reproducibility
  std::uniform_real_distribution<double> phase_dist(0.0, TWO_PI);

  size_t num_synth_frames = (num_frames > synth_fft) ? (num_frames - synth_fft) / synth_hop + 1 : 0;

  for (size_t h = 0; h < num_synth_frames; ++h) {
    double center_time =
        (static_cast<double>(h * synth_hop) + static_cast<double>(synth_fft) / 2.0) / sample_rate;

    // Find nearest stochastic frame
    size_t nearest = 0;
    double min_dist = std::abs(model.frames[0].time_seconds - center_time);
    for (size_t f = 1; f < model.frames.size(); ++f) {
      double dist = std::abs(model.frames[f].time_seconds - center_time);
      if (dist < min_dist) {
        min_dist = dist;
        nearest = f;
      }
    }

    const auto& env = model.frames[nearest].envelope;

    // Generate random-phase spectrum shaped by the envelope.
    // For each bin, set magnitude from the interpolated envelope
    // and randomize the phase.
    std::vector<double> real_part(synth_fft, 0.0);
    std::vector<double> imag_part(synth_fft, 0.0);

    for (size_t k = 0; k < synth_bins; ++k) {
      // Interpolate envelope value at this bin
      double env_pos = static_cast<double>(k) / static_cast<double>(synth_bins - 1) *
                       static_cast<double>(env.size() - 1);
      size_t env_lo = static_cast<size_t>(env_pos);
      size_t env_hi = std::min(env_lo + 1, env.size() - 1);
      double frac = env_pos - static_cast<double>(env_lo);
      double mag = env[env_lo] + frac * (env[env_hi] - env[env_lo]);

      double phi = phase_dist(rng);
      double re = mag * std::cos(phi);
      double im = mag * std::sin(phi);

      real_part[k] = re;
      imag_part[k] = im;

      // Mirror for negative frequencies (real signal)
      if (k > 0 && k < synth_fft / 2) {
        real_part[synth_fft - k] = re;
        imag_part[synth_fft - k] = -im;
      }
    }

    // Inverse DFT (naive — matches our forward DFT fallback)
    std::vector<double> time_block(synth_fft);
    for (size_t n = 0; n < synth_fft; ++n) {
      double sum = 0.0;
      for (size_t k = 0; k < synth_fft; ++k) {
        double angle = TWO_PI * static_cast<double>(k) * static_cast<double>(n) /
                       static_cast<double>(synth_fft);
        sum += real_part[k] * std::cos(angle) - imag_part[k] * std::sin(angle);
      }
      time_block[n] = sum / static_cast<double>(synth_fft);
    }

    // Apply window and overlap-add
    size_t start = h * synth_hop;
    for (size_t i = 0; i < synth_fft && start + i < num_frames; ++i) {
      output.at(0, start + i) += static_cast<Sample>(time_block[i] * window[i] * norm);
    }
  }

  return output;
}

}  // namespace analysis
}  // namespace sferic
