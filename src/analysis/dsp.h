#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/buffer.h"

namespace sferic::analysis {

// Domain-agnostic signal + statistics shared by detector and extractor.

// Linear-interpolation-free percentile.
// value at rank p(0–1) of the sorted series. 
// Takes by value — the sort is in-place on the copy.
double percentile(std::vector<double> v, double p);

// Percentile p over the trailing `win` samples ending at each index.
// Indices before the first full window repeat the first computed value.
std::vector<double> trailing_percentile(const std::vector<double>& a, size_t win, double p);

// Moving average over `window` samples, output length == input length. 
// Edge samples divide by the full window (partial sums).
std::vector<double> moving_average(const std::vector<double>& a, size_t window);

// Least-squares slope of y against x.
// 0 when fewer than 2 points or x is degenerate (all-equal).
double ols_slope(std::span<const double> x, std::span<const double> y);

// Least-squares slope of a uniformly sampled series, x is implicit (i × dx).
double ols_slope_uniform(std::span<const double> y, double dx);

// Otsu's method in log space.
// the split maximising between-cluster separation relative to within-cluster
// spread. Gated by `min_significance` (the between/within ratio) so a
// single-cluster series returns 0.0 . Non-positive and non-finite values are
// dropped.
double otsu_threshold(std::vector<double> values, double min_significance);

// The complement of Otsu for a low-tail minority.
// cut at the widest multiplicative gap in the lower half of the sorted series,
// and only when that gap dominates (≥ gap_factor × the median gap).
// Returns 0.0 when there is no clear valley.
double gap_valley_threshold(std::vector<double> values, double gap_factor);

// Per-frame RMS level in dBFS, mono-mixed across channels.
// Frames of `frame_len` samples step by `hop_len` (equal values give contiguous
// blocks), then `smooth` samples of moving average.
// Empty when the buffer is shorter than one frame.
std::vector<double> rms_envelope_db(const AudioBuffer& buffer, size_t frame_len, size_t hop_len,
                                    size_t smooth);

struct OnsetConfig {
  double rise_db = 12.0;          // dB above the trailing floor that opens an onset
  double hold_s = 0.05;           // time the level must stay above before it counts
  double refractory_s = 2.0;      // minimum gap between successive onsets
  double lookback_s = 1.0;        // trailing window the floor is read over
  double floor_percentile = 0.10; // percentile of that window taken as the floor
};

// Rising-edge crossings of (trailing floor + rise_db), debounced by hold_s and
// spaced by refractory_s. Returns indices into `env_db`.
std::vector<size_t> detect_onsets(const std::vector<double>& env_db, double hop_s,
                                  const OnsetConfig& cfg);

// Median dB over the `lookback_s` window ending `guard_s` before `index` — the
// local "returned to background" level.
double local_median_floor(const std::vector<double>& env_db, size_t index, double hop_s,
                          double lookback_s, double guard_s);

// Mean of every frame below the series median — the average of the quiet half
// of a playback. Lower and steadier than a per-event local floor when activity
// is near-continuous.
double quiet_half_mean(const std::vector<double>& env_db);

// Interaural cues. Mono input yields corr/coherence 1, width 0, balance 0.5.
struct StereoFrame {
  double corr = 1.0;       // L/R Pearson correlation at lag 0
  double width = 0.0;      // mid/side width — side / (mid + side)
  double coherence = 1.0;  // max L/R correlation over ITD lags
  double balance = 0.5;    // R / (L + R) energy — 0.5 centred
};

// Whole-buffer stereo summary.
// same four cues averaged over one window spanning the entire clip.
struct StereoProfile {
  double mean_corr = 1.0;       // mean L/R Pearson correlation at lag 0
  double mean_width = 0.0;      // mean mid/side width — side / (mid + side)
  double mean_balance = 0.5;    // mean R / (L + R) energy — 0.5 centred
  double mean_coherence = 1.0;  // mean max L/R correlation over ITD lags
};

// Cues over [s0, s1) of the buffer. 
// `max_itd_s` bounds the lag search for coherence (0.7ms = human interaural max).
StereoFrame stereo_window(const AudioBuffer& buffer, size_t s0, size_t s1, double max_itd_s);

// One StereoFrame per entry of `times_s`, each over `win_samples` from that time.
std::vector<StereoFrame> stereo_frames(const AudioBuffer& buffer, std::span<const double> times_s,
                                       size_t win_samples, double max_itd_s);

StereoProfile extract_stereo_profile(const AudioBuffer& buffer, double max_itd_s);

}  // namespace sferic::analysis
