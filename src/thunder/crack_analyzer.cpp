#include "thunder/crack_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "analysis/stft.h"
#include "analysis/stochastic_model.h"
#include "core/constants.h"
#include "thunder/lightning_model.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

const char* thunder_feature_name(ThunderFeature f) {
  switch (f) {
    case ThunderFeature::Unknown: return "Unknown";
    case ThunderFeature::Peak:    return "Peak";
    case ThunderFeature::Clap:    return "Clap";
    case ThunderFeature::Rumble:  return "Rumble";
  }
  return "Unknown";
}

CrackAnalyzer::CrackAnalyzer(const CrackAnalyzerConfig& config)
    : config_(config) {}

int64_t CrackAnalyzer::detect_onset(const SampleBuffer& audio,
                                     double sample_rate) const {
  if (audio.empty()) return -1;

  size_t rms_window = static_cast<size_t>(0.01 * sample_rate);  // 10 ms
  if (rms_window < 1) rms_window = 1;
  if (rms_window > audio.size()) return -1;

  // Compute background RMS from the first rms_window samples
  double bg_sum_sq = 0.0;
  for (size_t i = 0; i < rms_window; ++i) {
    bg_sum_sq += static_cast<double>(audio[i]) * static_cast<double>(audio[i]);
  }
  double bg_rms = std::sqrt(bg_sum_sq / static_cast<double>(rms_window));
  if (bg_rms < 1.0e-10) bg_rms = 1.0e-10;  // floor to avoid zero threshold

  double threshold = bg_rms * std::pow(10.0, config_.onset_threshold_db / 20.0);

  // Running RMS scan
  double sum_sq = bg_sum_sq;
  for (size_t i = rms_window; i < audio.size(); ++i) {
    // Slide the window: add new sample, remove oldest
    double new_s = static_cast<double>(audio[i]);
    double old_s = static_cast<double>(audio[i - rms_window]);
    sum_sq += new_s * new_s - old_s * old_s;
    if (sum_sq < 0.0) sum_sq = 0.0;  // numerical guard

    double rms = std::sqrt(sum_sq / static_cast<double>(rms_window));
    if (rms > threshold) {
      // Back up by pre_onset_ms to preserve the attack transient
      size_t pre_samples = static_cast<size_t>(config_.pre_onset_ms / 1000.0 * sample_rate);
      int64_t onset = static_cast<int64_t>(i) - static_cast<int64_t>(pre_samples);
      return std::max(onset, int64_t{0});
    }
  }
  return -1;
}

size_t CrackAnalyzer::find_crack_end(const SampleBuffer& audio,
                                      size_t onset_sample,
                                      double sample_rate) const {
  size_t max_end = onset_sample +
                   static_cast<size_t>(config_.crack_window_ms / 1000.0 * sample_rate);
  max_end = std::min(max_end, audio.size());

  // Find peak amplitude from onset
  float peak = 0.0f;
  size_t peak_idx = onset_sample;
  for (size_t i = onset_sample; i < max_end; ++i) {
    float abs_val = std::abs(audio[i]);
    if (abs_val > peak) {
      peak = abs_val;
      peak_idx = i;
    }
  }

  if (peak < 1.0e-10f) return max_end;

  // Decay floor relative to peak
  double floor_linear = static_cast<double>(peak) *
                        std::pow(10.0, config_.decay_floor_db / 20.0);

  // Running RMS scan from peak forward
  size_t rms_win = static_cast<size_t>(0.005 * sample_rate);  // 5 ms
  if (rms_win < 1) rms_win = 1;

  double sum_sq = 0.0;
  for (size_t i = peak_idx; i < std::min(peak_idx + rms_win, max_end); ++i) {
    sum_sq += static_cast<double>(audio[i]) * static_cast<double>(audio[i]);
  }

  for (size_t i = peak_idx + rms_win; i < max_end; ++i) {
    double new_s = static_cast<double>(audio[i]);
    double old_s = static_cast<double>(audio[i - rms_win]);
    sum_sq += new_s * new_s - old_s * old_s;
    if (sum_sq < 0.0) sum_sq = 0.0;

    double rms = std::sqrt(sum_sq / static_cast<double>(rms_win));
    if (rms < floor_linear) {
      return i;
    }
  }
  return max_end;
}

analysis::StochasticModel CrackAnalyzer::run_stochastic_fixed(
    const SampleBuffer& window, double sample_rate) const {
  // Build an AudioBuffer from the SampleBuffer
  AudioBuffer buf(1, window.size(), sample_rate);
  for (size_t i = 0; i < window.size(); ++i) {
    buf.at(0, i) = window[i];
  }

  analysis::StochasticModelConfig cfg;
  cfg.fft_size = config_.fft_size;
  cfg.hop_size = config_.hop_size;
  cfg.num_envelope_points = config_.num_envelope_points;
  cfg.lpc_order = 0;
  cfg.smoothing_bins = 5;

  analysis::StochasticAnalyzer analyzer(cfg, sample_rate);
  return analyzer.analyze(buf);
}

analysis::StochasticModel CrackAnalyzer::run_stochastic_multiresolution(
    const SampleBuffer& window, double sample_rate) const {
  // Build AudioBuffer
  AudioBuffer buf(1, window.size(), sample_rate);
  for (size_t i = 0; i < window.size(); ++i) {
    buf.at(0, i) = window[i];
  }

  // Three-pass MultiResolutionSTFT approximating S-transform
  analysis::MultiResConfig mr_config;
  double nyquist = sample_rate / 2.0;

  // LF pass: long window for frequency resolution below 100 Hz
  analysis::MultiResConfig::Pass lf_pass;
  lf_pass.stft.fft_size = config_.fft_size * 8;
  lf_pass.stft.hop_size = config_.fft_size * 2;
  lf_pass.stft.window_type = analysis::WindowType::HANN;
  lf_pass.freq_lo = 0.0;
  lf_pass.freq_hi = 100.0;
  mr_config.passes.push_back(lf_pass);

  // MF pass: medium window for 100–500 Hz
  analysis::MultiResConfig::Pass mf_pass;
  mf_pass.stft.fft_size = config_.fft_size * 2;
  mf_pass.stft.hop_size = config_.fft_size / 2;
  mf_pass.stft.window_type = analysis::WindowType::HANN;
  mf_pass.freq_lo = 100.0;
  mf_pass.freq_hi = 500.0;
  mr_config.passes.push_back(mf_pass);

  // HF pass: short window for temporal resolution above 500 Hz
  analysis::MultiResConfig::Pass hf_pass;
  hf_pass.stft.fft_size = config_.fft_size;
  hf_pass.stft.hop_size = config_.hop_size;
  hf_pass.stft.window_type = analysis::WindowType::HANN;
  hf_pass.freq_lo = 500.0;
  hf_pass.freq_hi = nyquist;
  mr_config.passes.push_back(hf_pass);

  analysis::MultiResolutionSTFT stft(mr_config, sample_rate);
  auto frames = stft.analyze(buf);

  // Convert MultiResFrames to StochasticModel
  analysis::StochasticModel model;
  model.sample_rate = sample_rate;
  model.num_envelope_points = config_.num_envelope_points;

  double freq_spacing = nyquist / static_cast<double>(config_.num_envelope_points - 1);

  for (const auto& frame : frames) {
    analysis::StochasticFrame sf;
    sf.time_seconds = frame.time_seconds;
    sf.frequency_spacing = freq_spacing;
    sf.envelope.resize(config_.num_envelope_points, 0.0);

    if (frame.frequencies.empty()) {
      model.frames.push_back(sf);
      continue;
    }

    // Interpolate from irregular frequency grid to fixed envelope points
    for (size_t j = 0; j < config_.num_envelope_points; ++j) {
      double target_freq = static_cast<double>(j) * freq_spacing;

      // Find bracketing entries in the merged frame
      size_t lo = 0;
      for (size_t k = 1; k < frame.frequencies.size(); ++k) {
        if (frame.frequencies[k] > target_freq) {
          lo = k - 1;
          break;
        }
        lo = k;
      }

      if (lo + 1 < frame.frequencies.size() &&
          frame.frequencies[lo + 1] > frame.frequencies[lo]) {
        double f0 = frame.frequencies[lo];
        double f1 = frame.frequencies[lo + 1];
        double frac = (target_freq - f0) / (f1 - f0);
        frac = std::clamp(frac, 0.0, 1.0);
        sf.envelope[j] = frame.magnitudes[lo] * (1.0 - frac) +
                         frame.magnitudes[lo + 1] * frac;
      } else if (!frame.magnitudes.empty()) {
        sf.envelope[j] = frame.magnitudes[lo];
      }
    }

    model.frames.push_back(sf);
  }

  return model;
}

analysis::StochasticModel CrackAnalyzer::run_stochastic(
    const SampleBuffer& window, double sample_rate) const {
  if (config_.use_multiresolution) {
    return run_stochastic_multiresolution(window, sample_rate);
  }
  return run_stochastic_fixed(window, sample_rate);
}

double CrackAnalyzer::estimate_spectral_peak(
    const analysis::StochasticModel& model, double sample_rate) const {
  if (model.frames.empty()) return 0.0;

  size_t num_pts = model.num_envelope_points;
  std::vector<double> avg_envelope(num_pts, 0.0);

  // Average across early frames (first third or all if short)
  size_t n_frames = std::max(size_t{1}, model.frames.size() / 3);
  for (size_t f = 0; f < n_frames; ++f) {
    const auto& env = model.frames[f].envelope;
    for (size_t j = 0; j < std::min(num_pts, env.size()); ++j) {
      avg_envelope[j] += env[j];
    }
  }
  for (size_t j = 0; j < num_pts; ++j) {
    avg_envelope[j] /= static_cast<double>(n_frames);
  }

  // Find peak bin
  size_t peak_bin = 0;
  double peak_val = 0.0;
  for (size_t j = 0; j < num_pts; ++j) {
    if (avg_envelope[j] > peak_val) {
      peak_val = avg_envelope[j];
      peak_bin = j;
    }
  }

  double freq_spacing = model.frames.empty()
                            ? (sample_rate / 2.0) / static_cast<double>(num_pts - 1)
                            : model.frames[0].frequency_spacing;

  return static_cast<double>(peak_bin) * freq_spacing;
}

ThunderFeature CrackAnalyzer::classify_feature(double spectral_peak_hz,
                                                double duration_ms) const {
  if (spectral_peak_hz < THUNDER_PEAK_FREQ_MIN_HZ) return ThunderFeature::Rumble;
  if (duration_ms < CLAP_DURATION_MIN_MS) return ThunderFeature::Peak;
  if (duration_ms <= CLAP_DURATION_MAX_MS) return ThunderFeature::Clap;
  return ThunderFeature::Rumble;
}

void CrackAnalyzer::measure_envelope(const SampleBuffer& window,
                                      double sample_rate,
                                      double& attack_ms,
                                      double& decay_ms) const {
  attack_ms = 0.0;
  decay_ms = 0.0;
  if (window.empty()) return;

  // Smoothed amplitude envelope (moving average, ~1 ms window)
  size_t smooth_win = std::max(size_t{1},
                               static_cast<size_t>(0.001 * sample_rate));
  std::vector<double> envelope(window.size(), 0.0);

  double running_sum = 0.0;
  for (size_t i = 0; i < window.size(); ++i) {
    running_sum += std::abs(static_cast<double>(window[i]));
    if (i >= smooth_win) {
      running_sum -= std::abs(static_cast<double>(window[i - smooth_win]));
    }
    size_t count = std::min(i + 1, smooth_win);
    envelope[i] = running_sum / static_cast<double>(count);
  }

  // Find peak
  double peak = *std::max_element(envelope.begin(), envelope.end());
  if (peak < 1.0e-10) return;

  double level_10 = peak * 0.1;
  double level_90 = peak * 0.9;

  // Attack: 10% → 90% rise time
  size_t idx_10_rise = 0, idx_90_rise = 0;
  for (size_t i = 0; i < envelope.size(); ++i) {
    if (envelope[i] >= level_10 && idx_10_rise == 0) idx_10_rise = i;
    if (envelope[i] >= level_90) { idx_90_rise = i; break; }
  }
  attack_ms = static_cast<double>(idx_90_rise - idx_10_rise) / sample_rate * 1000.0;

  // Find peak index
  size_t peak_idx = static_cast<size_t>(
      std::max_element(envelope.begin(), envelope.end()) - envelope.begin());

  // Decay: 90% → 10% fall time (from peak onward)
  size_t idx_90_fall = 0, idx_10_fall = 0;
  bool found_90 = false;
  for (size_t i = peak_idx; i < envelope.size(); ++i) {
    if (!found_90 && envelope[i] <= level_90) { idx_90_fall = i; found_90 = true; }
    if (found_90 && envelope[i] <= level_10) { idx_10_fall = i; break; }
  }
  if (found_90 && idx_10_fall > idx_90_fall) {
    decay_ms = static_cast<double>(idx_10_fall - idx_90_fall) / sample_rate * 1000.0;
  }
}

CrackAnalysisResult CrackAnalyzer::analyze(const SampleBuffer& mono_audio,
                                            double sample_rate) const {
  CrackAnalysisResult result;

  // 1. Onset detection
  int64_t onset = detect_onset(mono_audio, sample_rate);
  if (onset < 0) return result;

  result.onset_found = true;
  result.onset_sample = static_cast<size_t>(onset);

  // 2. Find crack end
  size_t crack_end = find_crack_end(mono_audio, result.onset_sample, sample_rate);

  // 3. Extract window
  SampleBuffer window(mono_audio.begin() + static_cast<ptrdiff_t>(result.onset_sample),
                       mono_audio.begin() + static_cast<ptrdiff_t>(crack_end));

  result.crack_duration_ms = static_cast<double>(crack_end - result.onset_sample) /
                             sample_rate * 1000.0;

  // 4. Copy source window
  result.source_window = window;

  // 5. Stochastic analysis
  result.crack_model = run_stochastic(window, sample_rate);

  // 6. Spectral peak
  result.spectral_peak_hz = estimate_spectral_peak(result.crack_model, sample_rate);

  // 7. Feature classification
  result.feature = classify_feature(result.spectral_peak_hz, result.crack_duration_ms);

  // 8. Envelope measurements
  measure_envelope(window, sample_rate, result.attack_ms, result.decay_ms);

  // 9. Normalise model to peak amplitude = 1.0
  if (config_.normalise && !result.crack_model.frames.empty()) {
    double max_val = 0.0;
    for (const auto& frame : result.crack_model.frames) {
      for (double v : frame.envelope) {
        max_val = std::max(max_val, v);
      }
    }
    if (max_val > 0.0) {
      double inv_max = 1.0 / max_val;
      for (auto& frame : result.crack_model.frames) {
        for (double& v : frame.envelope) {
          v *= inv_max;
        }
      }
    }
  }

  return result;
}

CrackAnalysisResult CrackAnalyzer::analyze(const AudioBuffer& audio) const {
  AudioBuffer mono = audio.to_mono();

  SampleBuffer samples(mono.num_frames());
  for (size_t i = 0; i < mono.num_frames(); ++i) {
    samples[i] = mono.at(0, i);
  }

  return analyze(samples, audio.sample_rate());
}

void print_crack_analysis(const CrackAnalysisResult& result) {
  if (!result.onset_found) {
    std::printf("No thunder onset detected.\n");
    return;
  }
  std::printf("=== Crack Analysis ===\n");
  std::printf("Feature:        %s\n", thunder_feature_name(result.feature));
  std::printf("Onset sample:   %zu\n", result.onset_sample);
  std::printf("Duration:       %.1f ms\n", result.crack_duration_ms);
  std::printf("Attack (10-90): %.2f ms\n", result.attack_ms);
  std::printf("Decay (90-10):  %.2f ms\n", result.decay_ms);
  std::printf("Spectral peak:  %.1f Hz\n", result.spectral_peak_hz);
  std::printf("Model frames:   %zu\n", result.crack_model.frames.size());
}

bool validate_against_physics(const CrackAnalysisResult& result,
                               double current_ka,
                               double tolerance_hz) {
  double expected = peak_frequency_hz(current_ka);
  return std::abs(result.spectral_peak_hz - expected) <= tolerance_hz;
}

}  // namespace thunder
}  // namespace sferic
