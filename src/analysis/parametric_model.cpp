#include "analysis/parametric_model.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "analysis/distribution_fitter.h"
#include "core/logger.h"

namespace sferic {
namespace analysis {

ParametricModel ParametricExtractor::extract(const SpectralEnvelope& model) const {
  SFERIC_SCOPE("ParametricExtractor::extract");

  ParametricModel result;
  result.sample_rate = model.sample_rate;
  result.duration_s = model.duration();
  result.source_frame_count = model.ms.size();
  result.num_bins = model.num_bins();

  {
    std::ostringstream ss;
    ss << "input: " << model.ms.size() << " ms-frames"
       << "  duration=" << std::fixed << std::setprecision(2) << model.duration() << "s"
       << "  model=" << log::fmt_bytes(model.size_bytes());
    SFERIC_LOG(Info, ss.str());
  }

  SFERIC_LOG(Info, "computing overall temporal envelope");
  result.overall_envelope = compute_temporal_envelope(model);
  {
    std::ostringstream ss;
    ss << "overall envelope: attack=" << std::fixed << std::setprecision(1)
       << result.overall_envelope.attack_time_s * 1000.0 << "ms"
       << "  peak=" << result.overall_envelope.peak_time_s * 1000.0 << "ms"
       << "  release=" << result.overall_envelope.release_time_s * 1000.0 << "ms"
       << "  noise_floor=" << std::setprecision(1) << result.overall_envelope.noise_floor_db
       << "dB";
    SFERIC_LOG(Info, ss.str());
  }

  SFERIC_LOG(Info, "computing band envelopes (variance-driven)");
  result.bands = compute_band_envelopes(model);
  {
    std::ostringstream ss;
    ss << "bands found: " << result.bands.size();
    for (size_t i = 0; i < result.bands.size(); ++i) {
      ss << "\n  band[" << i << "]  center=" << std::fixed << std::setprecision(0)
         << result.bands[i].center_hz << "Hz"
         << "  bw=" << result.bands[i].bandwidth_hz << "Hz";
    }
    SFERIC_LOG(Info, ss.str());
  }

  SFERIC_LOG(Info, "computing spectral shapes per ms-frame");
  result.spectral_trajectory.reserve(model.ms.size());
  for (size_t i = 0; i < model.ms.size(); ++i) {
    result.spectral_trajectory.push_back(compute_spectral_shape(model.ms[i]));
    SFERIC_PROGRESS(i, model.ms.size(), model.ms[i].time_seconds * 1000.0,
                    result.spectral_trajectory.size() * sizeof(SpectralShape));
  }

  {
    SpectralShape mean{};
    const double inv_n = 1.0 / static_cast<double>(result.spectral_trajectory.size());
    for (const SpectralShape& s : result.spectral_trajectory) {
      mean.centroid_hz += s.centroid_hz * inv_n;
      mean.spread_hz += s.spread_hz * inv_n;
      mean.tilt_db_octave += s.tilt_db_octave * inv_n;
      mean.flatness += s.flatness * inv_n;
      mean.rolloff_hz += s.rolloff_hz * inv_n;
    }
    result.mean_spectral_shape = mean;

    std::ostringstream ss;
    ss << "mean spectral shape: centroid=" << std::fixed << std::setprecision(1) << mean.centroid_hz
       << "Hz"
       << "  spread=" << mean.spread_hz << "Hz"
       << "  tilt=" << std::setprecision(2) << mean.tilt_db_octave << "dB/oct"
       << "  flatness=" << mean.flatness << "  rolloff=" << std::setprecision(1) << mean.rolloff_hz
       << "Hz";
    SFERIC_LOG(Info, ss.str());
  }

  SFERIC_LOG(Info, "fitting top-level distributions (centroid, spread, amplitude)");
  {
    std::vector<double> centroids, spreads, amplitudes;
    centroids.reserve(model.ms.size());
    spreads.reserve(model.ms.size());
    amplitudes.reserve(model.ms.size());

    for (size_t i = 0; i < model.ms.size(); ++i) {
      centroids.push_back(result.spectral_trajectory[i].centroid_hz);
      spreads.push_back(result.spectral_trajectory[i].spread_hz);
      const auto& env = model.ms[i].envelope;
      amplitudes.push_back(std::accumulate(env.begin(), env.end(), 0.0));
    }

    result.centroid_distribution = fit_distribution(centroids);
    result.spread_distribution = fit_distribution(spreads);
    result.amplitude_distribution = fit_distribution(amplitudes);
  }

  SFERIC_LOG(Info, "computing peak tracks");
  result.peak_tracks = track_spectral_peaks(model);
  SFERIC_LOG(Info, "peak tracks: " + std::to_string(result.peak_tracks.size()));

  return result;
}

SpectralShape ParametricExtractor::compute_spectral_shape(const EnvelopeFrame& frame) const {
  SpectralShape shape{};

  const std::vector<double>& env = frame.envelope;
  const size_t n = env.size();

  // Centroid and spread
  double weight_sum = 0.0;
  double weighted_f_sum = 0.0;

  for (size_t i = 0; i < n; ++i) {
    const double freq = static_cast<double>(i) * frame.frequency_spacing;
    weight_sum += env[i];
    weighted_f_sum += freq * env[i];
  }

  if (weight_sum < 1e-12) return shape;
  shape.centroid_hz = weighted_f_sum / weight_sum;

  double variance_sum = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double freq = static_cast<double>(i) * frame.frequency_spacing;
    const double diff = freq - shape.centroid_hz;
    variance_sum += diff * diff * env[i];
  }
  shape.spread_hz = std::sqrt(variance_sum / weight_sum);

  // Spectral tilt (dB/octave)
  // OLS fit of power-dB vs log₂(Hz). Skips DC and near-silent bins.
  constexpr double kMinMag = 1e-10;
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  size_t tilt_count = 0;

  for (size_t i = 1; i < n; ++i) {
    if (env[i] < kMinMag) continue;
    const double x = std::log2(static_cast<double>(i) * frame.frequency_spacing);
    const double y = 20.0 * std::log10(env[i]);
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    ++tilt_count;
  }

  if (tilt_count >= 2) {
    const double denom = static_cast<double>(tilt_count) * sxx - sx * sx;
    if (std::abs(denom) > 1e-12)
      shape.tilt_db_octave = (static_cast<double>(tilt_count) * sxy - sx * sy) / denom;
  }

  // Spectral flatness (Wiener entropy)
  // 0 = tonal, 1 = white noise.
  double log_power_sum = 0.0;
  double arith_power = 0.0;
  size_t flat_count = 0;

  for (size_t i = 0; i < n; ++i) {
    if (env[i] < kMinMag) continue;
    const double power = env[i] * env[i];
    log_power_sum += std::log(power);
    arith_power += power;
    ++flat_count;
  }

  if (flat_count > 0 && arith_power > 1e-24) {
    const double geom_mean = std::exp(log_power_sum / static_cast<double>(flat_count));
    const double arith_mean = arith_power / static_cast<double>(flat_count);
    shape.flatness = std::clamp(geom_mean / arith_mean, 0.0, 1.0);
  }

  // Rolloff frequency (85% energy)
  constexpr double kRolloffThreshold = 0.85;

  double total_power = 0.0;
  for (size_t i = 0; i < n; ++i) total_power += env[i] * env[i];

  if (total_power > 1e-24) {
    double cumulative = 0.0;
    const double target = kRolloffThreshold * total_power;
    for (size_t i = 0; i < n; ++i) {
      cumulative += env[i] * env[i];
      if (cumulative >= target) {
        shape.rolloff_hz = static_cast<double>(i) * frame.frequency_spacing;
        break;
      }
    }
  }

  return shape;
}

// Boxcar (flat) moving average with clamped edges.
static std::vector<double> moving_average(const std::vector<double>& v, size_t half_window) {
  const size_t n = v.size();
  std::vector<double> out(n);
  for (size_t i = 0; i < n; ++i) {
    const size_t lo = (i >= half_window) ? i - half_window : 0;
    const size_t hi = std::min(i + half_window + 1, n);
    double sum = 0.0;
    for (size_t j = lo; j < hi; ++j) sum += v[j];
    out[i] = sum / static_cast<double>(hi - lo);
  }
  return out;
}

// Douglas-Peucker 1-D reduction — indices are implicit (0..v.size()-1).
static void rdp_reduce(const std::vector<double>& v, size_t lo, size_t hi, double tolerance,
                       std::vector<size_t>& out) {
  if (hi <= lo + 1) return;
  double max_dev = 0.0;
  size_t max_idx = lo + 1;
  const double span = static_cast<double>(hi - lo);
  for (size_t i = lo + 1; i < hi; ++i) {
    const double t = static_cast<double>(i - lo) / span;
    const double dev = std::abs(v[i] - (v[lo] + t * (v[hi] - v[lo])));
    if (dev > max_dev) {
      max_dev = dev;
      max_idx = i;
    }
  }
  if (max_dev > tolerance) {
    rdp_reduce(v, lo, max_idx, tolerance, out);
    out.push_back(max_idx);
    rdp_reduce(v, max_idx, hi, tolerance, out);
  }
}

// Returns sorted knot indices [0, interior..., n-1] for a DPR-reduced piecewise-linear fit.
static std::vector<size_t> build_macro_knots(const std::vector<double>& smooth, double tolerance) {
  std::vector<size_t> interior;
  rdp_reduce(smooth, 0, smooth.size() - 1, tolerance, interior);
  std::sort(interior.begin(), interior.end());
  std::vector<size_t> knots;
  knots.reserve(interior.size() + 2);
  knots.push_back(0);
  for (size_t idx : interior) knots.push_back(idx);
  knots.push_back(smooth.size() - 1);
  return knots;
}

TemporalEnvelope ParametricExtractor::compute_temporal_envelope_from_rms(
    const std::vector<double>& rms, const std::vector<double>& times) const {
  SFERIC_SCOPE("compute_temporal_envelope_from_rms");

  TemporalEnvelope result{};
  const size_t n = rms.size();

  SFERIC_LOG(Info, "n=" + std::to_string(n) + " rms samples");

  result.rms_trajectory = rms;

  const auto peak_it = std::max_element(rms.begin(), rms.end());
  const double peak_rms = *peak_it;

  constexpr double kAttackLo = 0.1;
  constexpr double kAttackHi = 0.9;
  constexpr double kReleaseHi = 0.9;
  constexpr double kReleaseLo = 0.1;

  const size_t peak_idx = static_cast<size_t>(peak_it - rms.begin());
  result.peak_time_s = times[peak_idx] - times[0];

  // Attack
  size_t attack_lo_idx = 0;
  size_t attack_hi_idx = peak_idx;

  for (size_t i = 0; i < peak_idx; ++i) {
    if (rms[i] >= kAttackLo * peak_rms) {
      attack_lo_idx = i;
      break;
    }
  }
  for (size_t i = attack_lo_idx; i < peak_idx; ++i) {
    if (rms[i] >= kAttackHi * peak_rms) {
      attack_hi_idx = i;
      break;
    }
  }
  result.attack_time_s = times[attack_hi_idx] - times[attack_lo_idx];

  // Release
  size_t release_hi_idx = peak_idx;
  size_t release_lo_idx = n - 1;

  for (size_t i = peak_idx; i < n; ++i) {
    if (rms[i] <= kReleaseHi * peak_rms) {
      release_hi_idx = i;
      break;
    }
  }
  for (size_t i = release_hi_idx; i < n; ++i) {
    if (rms[i] <= kReleaseLo * peak_rms) {
      release_lo_idx = i;
      break;
    }
  }
  result.release_time_s = times[release_lo_idx] - times[release_hi_idx];

  // Sustain
  if (release_hi_idx > attack_hi_idx) {
    double sum = 0.0;
    for (size_t i = attack_hi_idx; i < release_hi_idx; ++i) sum += rms[i];
    result.sustain_level = (sum / static_cast<double>(release_hi_idx - attack_hi_idx)) / peak_rms;
  }

  // Noise floor
  double floor_sum = 0.0;
  size_t floor_count = 0;
  for (size_t i = 0; i < attack_lo_idx; ++i) {
    floor_sum += rms[i];
    ++floor_count;
  }
  for (size_t i = release_lo_idx + 1; i < n; ++i) {
    floor_sum += rms[i];
    ++floor_count;
  }

  if (floor_count > 0) {
    const double floor_rms = floor_sum / static_cast<double>(floor_count);
    result.noise_floor_db = (floor_rms > 1e-12) ? 20.0 * std::log10(floor_rms / peak_rms) : -120.0;
  } else {
    result.noise_floor_db = -120.0;
  }

  {
    std::ostringstream ss;
    ss << "attack=" << std::fixed << std::setprecision(1) << result.attack_time_s * 1000.0 << "ms"
       << "  peak@" << result.peak_time_s * 1000.0 << "ms"
       << "  release=" << result.release_time_s * 1000.0 << "ms"
       << "  sustain=" << std::setprecision(3) << result.sustain_level
       << "  floor=" << std::setprecision(1) << result.noise_floor_db << "dB";
    SFERIC_LOG(Info, ss.str());
  }

  // Smooth with a 100ms half-window to remove rapid spikes, then compress to the
  // minimal knot set that reconstructs the smooth within 0.5% of peak (DPR).
  const double frame_dt =
      (n > 1) ? (times.back() - times.front()) / static_cast<double>(n - 1) : 0.001;
  const size_t smooth_half = static_cast<size_t>(std::round(0.1 / frame_dt));  // 100ms — empirical
  const std::vector<double> smooth = moving_average(rms, smooth_half);
  const double smooth_peak = *std::max_element(smooth.begin(), smooth.end());

  {
    constexpr double kMacroTolerance = 0.005;  // empirical — 0.5% of peak
    const std::vector<size_t> knots = build_macro_knots(smooth, kMacroTolerance * smooth_peak);
    result.macro_shape.kind = DistributionKind::PiecewiseLinear;  // TODO fit or model
    result.macro_shape.sample_count = n;
    result.macro_shape.ks_p_value = 1.0;         // encoding, not a statistical fit
    result.macro_shape.params[0] = smooth_peak;  // absolute scale — knot values are peak-normalised
    const double n_norm = static_cast<double>(n - 1);
    result.macro_shape.weights.resize(knots.size() * 2);
    for (size_t k = 0; k < knots.size(); ++k) {
      result.macro_shape.weights[2 * k] = static_cast<double>(knots[k]) / n_norm;
      result.macro_shape.weights[2 * k + 1] = smooth[knots[k]] / smooth_peak;
    }

    {
      std::ostringstream ss;
      ss << "macro_shape: " << knots.size() << " knots"
         << "  smooth_peak=" << std::scientific << std::setprecision(3) << smooth_peak;
      SFERIC_LOG(Info, ss.str());
    }
  }

  // rms[i] / smooth[i] — multiplicative deviation from macro contour.
  // Expected Lognormal or Gamma — always positive, right-skewed, ~1.0 mean.
  constexpr double kResidualFloor = 1e-8;  // empirical
  std::vector<double> residuals;
  residuals.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (smooth[i] > kResidualFloor && rms[i] > kResidualFloor)
      residuals.push_back(rms[i] / smooth[i]);
  }
  if (residuals.size() >= 4) result.residual_distribution = fit_distribution(residuals);

  // Lag-1 autocorrelation φ of the residual series — texture wander rate.
  // φ→0 = white flicker (independent each frame); φ→1 = slow drift.
  // Reproduced at synthesis as AR(1): y[i] = μ + φ(y[i-1]−μ) + √(1−φ²)(s[i]−μ).
  if (residuals.size() >= 2) {
    const double r_mean = std::accumulate(residuals.begin(), residuals.end(), 0.0) /
                          static_cast<double>(residuals.size());
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i + 1 < residuals.size(); ++i)
      num += (residuals[i] - r_mean) * (residuals[i + 1] - r_mean);
    for (double r : residuals) {
      const double d = r - r_mean;
      den += d * d;
    }
    result.residual_autocorr = (den > 1e-24) ? std::clamp(num / den, 0.0, 0.999) : 0.0;
    {
      std::ostringstream ss;
      ss << "residual_autocorr φ=" << std::fixed << std::setprecision(4)
         << result.residual_autocorr;
      SFERIC_LOG(Info, ss.str());
    }
  }

  // Impulsive spikes = local maxima in full_res above mean + 1.5σ.
  // Inter-arrival times → Exponential for a Poisson process, Weibull for bursty.
  if (residuals.size() >= 4) {
    const double r_mean = std::accumulate(residuals.begin(), residuals.end(), 0.0) /
                          static_cast<double>(residuals.size());
    double r_var = 0.0;
    for (double r : residuals) {
      double d = r - r_mean;
      r_var += d * d;
    }
    const double r_std = std::sqrt(r_var / static_cast<double>(residuals.size()));
    constexpr double kSpikeK = 1.5;  // empirical — mean + 1.5σ
    const double spike_thresh = r_mean + kSpikeK * r_std;

    std::vector<double> full_res(n, 1.0);
    for (size_t i = 0; i < n; ++i)
      if (smooth[i] > kResidualFloor) full_res[i] = rms[i] / smooth[i];

    std::vector<double> spike_times_v;
    for (size_t i = 1; i + 1 < n; ++i) {
      if (full_res[i] > spike_thresh && full_res[i] >= full_res[i - 1] &&
          full_res[i] >= full_res[i + 1])
        spike_times_v.push_back(times[i]);
    }

    std::vector<double> intervals;
    intervals.reserve(spike_times_v.size());
    for (size_t i = 1; i < spike_times_v.size(); ++i)
      intervals.push_back(spike_times_v[i] - spike_times_v[i - 1]);

    if (intervals.size() >= 4) {
      result.spike_interval_distribution = fit_distribution(intervals);
      std::ostringstream ss;
      ss << "spike_intervals: " << intervals.size() << " spikes"
         << "  dist=" << static_cast<int>(result.spike_interval_distribution.kind);
      SFERIC_LOG(Info, ss.str());
    }
  }

  return result;
}

TemporalEnvelope ParametricExtractor::compute_temporal_envelope(
    const SpectralEnvelope& model) const {
  SFERIC_SCOPE("compute_temporal_envelope");

  const size_t n = model.ms.size();
  SFERIC_LOG(Info, "building RMS trajectory over " + std::to_string(n) + " ms-frames");

  std::vector<double> rms(n);
  std::vector<double> times(n);

  for (size_t i = 0; i < n; ++i) {
    const auto& env = model.ms[i].envelope;
    times[i] = model.ms[i].time_seconds;
    double sum_sq = 0.0;
    for (double v : env) sum_sq += v * v;
    rms[i] = std::sqrt(sum_sq / static_cast<double>(env.size()));
  }

  return compute_temporal_envelope_from_rms(rms, times);
}

SpectralShape ParametricExtractor::compute_spectral_shape_band(const EnvelopeFrame& frame,
                                                               size_t bin_lo, size_t bin_hi) const {
  // Build a masked frame: zero outside [bin_lo, bin_hi] so compute_spectral_shape
  // computes all metrics (centroid, tilt, flatness, rolloff) within the band only.
  const size_t n = frame.envelope.size();
  EnvelopeFrame masked;
  masked.time_seconds = frame.time_seconds;
  masked.frequency_spacing = frame.frequency_spacing;
  masked.envelope.assign(n, 0.0);
  for (size_t b = bin_lo; b <= bin_hi && b < n; ++b) masked.envelope[b] = frame.envelope[b];
  return compute_spectral_shape(masked);
}

std::vector<BandEnvelope> ParametricExtractor::compute_band_envelopes(
    const SpectralEnvelope& model) const {
  SFERIC_SCOPE("compute_band_envelopes");

  const size_t n_frames = model.ms.size();
  const size_t n_bins = model.num_bins();
  if (n_frames == 0 || n_bins == 0) return {};
  const double freq_spacing = model.ms[0].frequency_spacing;

  {
    std::ostringstream ss;
    ss << n_frames << " ms-frames  " << n_bins << " bins  " << std::fixed << std::setprecision(2)
       << freq_spacing << " Hz/bin";
    SFERIC_LOG(Info, ss.str());
  }

  // Per-bin peak and mean (two passes, no matrix storage)
  std::vector<double> bin_peak(n_bins, 0.0);
  std::vector<double> bin_mean(n_bins, 0.0);
  for (const EnvelopeFrame& frame : model.ms) {
    const size_t valid = std::min(n_bins, frame.envelope.size());
    for (size_t b = 0; b < valid; ++b) {
      bin_peak[b] = std::max(bin_peak[b], frame.envelope[b]);
      bin_mean[b] += frame.envelope[b];
    }
  }
  for (double& m : bin_mean) m /= static_cast<double>(n_frames);
  SFERIC_LOG_MEM(Info, "peak/mean arrays", n_bins * sizeof(double) * 2);

  const double total_energy = std::accumulate(bin_mean.begin(), bin_mean.end(), 0.0);

  // Movement has two independent components:
  //
  // Shape: normalised temporal profile distance — how the timing of rises/falls
  //   differs between adjacent bins, independent of absolute level.
  //   pa[f] = envelope[b][f] / peak[b];  shape_dist[b] = RMS(pa - pb)
  //
  // Gain: log-ratio standard deviation — how the RELATIVE AMPLITUDE between
  //   adjacent bins drifts over time.  If bin b decays faster than bin b+1 in dB,
  //   their ratio shifts frame-by-frame; normalised shapes look identical but they
  //   clearly belong to different spectral regions.
  //   gain_dist[b] = std( log(env[b][f] / env[b+1][f]) )
  //
  // Each metric is z-score normalised to its own distribution before combining.
  // This guarantees that adding a metric can only ADD band boundaries — a boundary
  // fires when EITHER shape OR gain z-score exceeds the sigma threshold.
  // (Raw-unit L2 combination inflates the threshold proportionally and may reduce
  // boundary count, which is the wrong direction.)

  const size_t n_adj = n_bins > 1 ? n_bins - 1 : 0;
  std::vector<double> shape_d(n_adj, 0.0);         // RMS shape distance per adjacent pair
  std::vector<double> log_ratio_mean(n_adj, 0.0);  // mean log-ratio for variance pass
  std::vector<double> gain_d(n_adj, 0.0);          // std of log-ratio per adjacent pair

  // Pass 1 — shape distance and log-ratio sum (for mean in pass 2)
  for (const EnvelopeFrame& frame : model.ms) {
    const size_t valid = std::min(n_bins, frame.envelope.size());
    for (size_t b = 0; b + 1 < valid; ++b) {
      const double a = frame.envelope[b];
      const double bb = frame.envelope[b + 1];
      const double pa = (bin_peak[b] > 1e-12) ? a / bin_peak[b] : 0.0;
      const double pb = (bin_peak[b + 1] > 1e-12) ? bb / bin_peak[b + 1] : 0.0;
      const double sd = pa - pb;
      shape_d[b] += sd * sd;
      if (a > 1e-12 && bb > 1e-12) log_ratio_mean[b] += std::log(a / bb);
    }
  }
  const double inv_n = 1.0 / static_cast<double>(n_frames);
  for (size_t b = 0; b < n_adj; ++b) {
    shape_d[b] = std::sqrt(shape_d[b] * inv_n);  // RMS
    log_ratio_mean[b] *= inv_n;                  // mean log-ratio
  }

  // Pass 2 — log-ratio variance → gain_d
  for (const EnvelopeFrame& frame : model.ms) {
    const size_t valid = std::min(n_bins, frame.envelope.size());
    for (size_t b = 0; b + 1 < valid; ++b) {
      const double a = frame.envelope[b];
      const double bb = frame.envelope[b + 1];
      if (a > 1e-12 && bb > 1e-12) {
        const double d = std::log(a / bb) - log_ratio_mean[b];
        gain_d[b] += d * d;
      }
    }
  }
  for (size_t b = 0; b < n_adj; ++b) gain_d[b] = std::sqrt(gain_d[b] * inv_n);

  // Each metric (shape, gain) is smoothed and thresholded independently using
  // mean + kSigma × std of its own distribution.  Detected boundary bins are
  // unioned across all metrics.  This guarantees adding a metric can only ADD
  // boundaries — a shape boundary fires regardless of what gain does, and a
  // gain-only boundary is detected on top of that.
  //
  // kSigma controls parsimony: higher → fewer, more significant bands.
  // The band count is determined entirely by the data — no grid is assumed.

  constexpr double kSigma = 2.0;             // empirical — 2σ above mean dissimilarity
  constexpr double kMinEnergyFrac = 0.0005;  // empirical — discard true silence
  constexpr size_t kSmoothWindow = 5;

  // Lambda: smooth a non-negative metric and find the peak-within-each-exceedance
  // boundary bins.  Returns a sorted set of bin indices that mark band transitions.
  const auto find_boundaries = [&](const std::vector<double>& metric,
                                   std::string_view label) -> std::vector<size_t> {
    const size_t m = metric.size();
    if (m == 0) return {};

    // Moving-average smoothing
    std::vector<double> smooth(m, 0.0);
    for (size_t b = 0; b < m; ++b) {
      const size_t lo = (b >= kSmoothWindow / 2) ? b - kSmoothWindow / 2 : 0;
      const size_t hi = std::min(b + kSmoothWindow / 2, m - 1);
      double s = 0.0;
      for (size_t j = lo; j <= hi; ++j) s += metric[j];
      smooth[b] = s / static_cast<double>(hi - lo + 1);
    }

    // Sigma threshold from smoothed distribution
    double mean_v = 0.0;
    for (double v : smooth) mean_v += v;
    mean_v /= static_cast<double>(m);
    double var_v = 0.0;
    for (double v : smooth) {
      const double e = v - mean_v;
      var_v += e * e;
    }
    var_v /= static_cast<double>(m);
    const double threshold = mean_v + kSigma * std::sqrt(var_v);

    {
      std::ostringstream ss;
      ss << label << ": mean=" << std::fixed << std::setprecision(4) << mean_v
         << "  std=" << std::sqrt(var_v) << "  threshold=" << threshold;
      SFERIC_LOG(Info, ss.str());
    }

    // Walk the smoothed metric — locate the peak within each above-threshold run
    std::vector<size_t> bins;
    bool above = false;
    double run_peak = 0.0;
    size_t run_peak_b = 0;
    for (size_t b = 0; b < m; ++b) {
      if (!above && smooth[b] >= threshold) {
        above = true;
        run_peak = smooth[b];
        run_peak_b = b;
      } else if (above) {
        if (smooth[b] > run_peak) {
          run_peak = smooth[b];
          run_peak_b = b;
        }
        if (smooth[b] < threshold) {
          bins.push_back(run_peak_b + 1);
          above = false;
        }
      }
    }
    return bins;
  };

  // Run detection on each metric independently, then union
  std::vector<size_t> shape_boundaries = find_boundaries(shape_d, "shape movement");
  std::vector<size_t> gain_boundaries = find_boundaries(gain_d, "gain movement");

  // Union: merge sorted lists, deduplicate adjacent bins (within 2 bins = same boundary)
  std::vector<size_t> all_boundaries;
  all_boundaries.push_back(1);  // start after DC
  {
    std::vector<size_t> merged;
    std::merge(shape_boundaries.begin(), shape_boundaries.end(), gain_boundaries.begin(),
               gain_boundaries.end(), std::back_inserter(merged));
    for (size_t b : merged) {
      if (all_boundaries.empty() || b > all_boundaries.back() + 2)
        all_boundaries.push_back(b);
      else
        all_boundaries.back() = std::max(all_boundaries.back(), b);  // keep farther boundary
    }
  }
  all_boundaries.push_back(n_bins - 1);

  struct Region {
    size_t start;
    size_t end;
  };
  std::vector<Region> candidates;
  for (size_t i = 0; i + 1 < all_boundaries.size(); ++i)
    candidates.push_back({all_boundaries[i], all_boundaries[i + 1]});

  {
    std::ostringstream ss;
    ss << "boundaries: shape=" << shape_boundaries.size() << "  gain=" << gain_boundaries.size()
       << "  union=" << (all_boundaries.size() - 2) << "  regions=" << candidates.size();
    SFERIC_LOG(Info, ss.str());
  }

  // Build a mean frame for computing per-band SpectralShape
  EnvelopeFrame mean_frame;
  mean_frame.frequency_spacing = freq_spacing;
  mean_frame.time_seconds = 0.0;
  mean_frame.envelope = bin_mean;

  // Filter and build BandEnvelopes
  std::vector<double> frame_times(n_frames);
  for (size_t f = 0; f < n_frames; ++f) frame_times[f] = model.ms[f].time_seconds;

  std::vector<BandEnvelope> result;

  for (const Region& r : candidates) {
    double region_energy = 0.0;
    for (size_t b = r.start; b <= r.end; ++b) region_energy += bin_mean[b];
    const double fraction = (total_energy > 1e-24) ? region_energy / total_energy : 0.0;
    if (fraction < kMinEnergyFrac) continue;

    const double f_lo = static_cast<double>(r.start) * freq_spacing;
    const double f_hi = static_cast<double>(r.end) * freq_spacing;

    {
      std::ostringstream ss;
      ss << "band accepted: " << std::fixed << std::setprecision(1) << f_lo << "–" << f_hi << "Hz"
         << "  energy=" << std::setprecision(2) << fraction * 100.0 << "%";
      SFERIC_LOG(Info, ss.str());
    }

    const size_t region_len = r.end - r.start + 1;
    std::vector<double> band_rms(n_frames, 0.0);
    std::vector<double> band_spread(n_frames, 0.0);
    std::vector<double> band_q(n_frames, 0.0);

    for (size_t f = 0; f < n_frames; ++f) {
      const auto& env = model.ms[f].envelope;

      // Gain — RMS of envelope values within this band
      double sum_sq = 0.0;
      for (size_t b = r.start; b <= r.end && b < env.size(); ++b) sum_sq += env[b] * env[b];
      band_rms[f] = std::sqrt(sum_sq / static_cast<double>(region_len));

      // Spectral centroid within the band (weighted mean frequency)
      double wsum = 0.0, fsum = 0.0;
      for (size_t b = r.start; b <= r.end && b < env.size(); ++b) {
        const double freq = static_cast<double>(b) * freq_spacing;
        wsum += env[b];
        fsum += freq * env[b];
      }
      if (wsum > 1e-12) {
        const double centroid = fsum / wsum;

        // Spread (standard deviation of frequency around centroid, weighted by amplitude)
        double var = 0.0;
        for (size_t b = r.start; b <= r.end && b < env.size(); ++b) {
          const double freq = static_cast<double>(b) * freq_spacing;
          const double d = freq - centroid;
          var += d * d * env[b];
        }
        const double spread = std::sqrt(var / wsum);
        band_spread[f] = spread;
        band_q[f] = (spread > 1e-12) ? centroid / spread : 0.0;
      }
    }

    BandEnvelope band;
    band.bin_lo = r.start;
    band.bin_hi = r.end;
    band.center_hz = std::sqrt(f_lo * f_hi);
    band.bandwidth_hz = f_hi - f_lo;
    band.energy_fraction = fraction;
    band.mean_shape = compute_spectral_shape_band(mean_frame, r.start, r.end);
    band.envelope = compute_temporal_envelope_from_rms(band_rms, frame_times);
    band.spread_trajectory = std::move(band_spread);
    band.q_trajectory = std::move(band_q);
    result.push_back(std::move(band));
  }

  SFERIC_LOG(Info, "bands kept: " + std::to_string(result.size()));
  return result;
}

std::vector<SpectralPeakTrack> ParametricExtractor::track_spectral_peaks(
    const SpectralEnvelope& model) const {
  SFERIC_SCOPE("track_spectral_peaks");

  const size_t n_frames = model.ms.size();
  const size_t n_bins = model.num_bins();
  if (n_frames == 0 || n_bins == 0) return {};

  const double freq_spacing = model.ms.front().frequency_spacing;

  constexpr double kMinMag = 1e-10;          // empirical — matches compute_spectral_shape
  constexpr double kMinAmplitudeDB = -80.0;  // empirical
  constexpr double kMinProminenceDB = 3.0;   // empirical — 3 dB above local saddle point
  constexpr size_t kMaxFreqShiftBins = 3;    // empirical — generous margin for ~1ms frames
  constexpr size_t kMinTrackFrames = 10;     // empirical — discard tracks shorter than 10ms
  const double kMaxFreqShiftHz = static_cast<double>(kMaxFreqShiftBins) * freq_spacing;

  {
    std::ostringstream ss;
    ss << n_frames << " frames  " << n_bins << " bins  " << std::fixed << std::setprecision(2)
       << freq_spacing << " Hz/bin"
       << "  max_freq_shift=" << kMaxFreqShiftHz << "Hz"
       << "  min_prominence=" << kMinProminenceDB << "dB"
       << "  min_track=" << kMinTrackFrames << " frames";
    SFERIC_LOG(Info, ss.str());
  }

  // per-track accumulator
  struct TrackAccum {
    double birth_time_s;
    double last_freq_hz;
    std::vector<double> times;
    std::vector<double> freqs_hz;
    std::vector<double> amps_db;
    double peak_amp_db = -1e300;
    size_t peak_frame = 0;  // absolute frame index in model.ms
    size_t peak_bin = 0;    // bin with max amplitude within that frame
  };

  std::vector<TrackAccum> active;
  std::vector<SpectralPeakTrack> completed;

  // finalize helper
  const auto finalize = [&](TrackAccum& acc) {
    if (acc.times.size() < kMinTrackFrames) return;

    SpectralPeakTrack t{};
    t.birth_time_s = acc.birth_time_s;
    t.death_time_s = acc.times.back();
    t.initial_frequency_hz = acc.freqs_hz.front();
    t.initial_amplitude_db = acc.amps_db.front();

    // Q factor — −3 dB bandwidth around the peak-amplitude bin
    const std::vector<double>& peak_env = model.ms[acc.peak_frame].envelope;
    const size_t pb = acc.peak_bin;
    const double thr = peak_env[pb] / std::sqrt(2.0);  // −3 dB in linear magnitude

    size_t lo = pb, hi = pb;
    while (lo > 0 && peak_env[lo - 1] >= thr) --lo;
    while (hi + 1 < peak_env.size() && peak_env[hi + 1] >= thr) ++hi;

    const double bw = static_cast<double>(hi - lo + 1) * freq_spacing;
    const double cf = static_cast<double>(pb) * freq_spacing;
    t.q_factor = (bw > 1e-9) ? cf / bw : 0.0;

    // Linear drift — OLS on (time_s, freq_hz)
    const size_t n = acc.times.size();
    const double dn = static_cast<double>(n);
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (size_t i = 0; i < n; ++i) {
      sx += acc.times[i];
      sy += acc.freqs_hz[i];
      sxx += acc.times[i] * acc.times[i];
      sxy += acc.times[i] * acc.freqs_hz[i];
    }
    const double ols_denom = dn * sxx - sx * sx;
    if (std::abs(ols_denom) > 1e-12) t.frequency_drift_hz_per_s = (dn * sxy - sx * sy) / ols_denom;
    const double intercept = (sy - t.frequency_drift_hz_per_s * sx) / dn;

    // Frequency jitter — residuals from the linear drift
    std::vector<double> jitter(n);
    for (size_t i = 0; i < n; ++i)
      jitter[i] = acc.freqs_hz[i] - (intercept + t.frequency_drift_hz_per_s * acc.times[i]);
    t.frequency_jitter = fit_distribution(jitter);

    // Amplitude variation — frame-to-frame deltas in dB
    if (n >= 2) {
      std::vector<double> deltas(n - 1);
      for (size_t i = 0; i < n - 1; ++i) deltas[i] = acc.amps_db[i + 1] - acc.amps_db[i];
      t.amplitude_variation = fit_distribution(deltas);
    }

    completed.push_back(std::move(t));
  };

  for (size_t fi = 0; fi < n_frames; ++fi) {
    const EnvelopeFrame& frame = model.ms[fi];
    const size_t n_fb = frame.envelope.size();
    const double t_frame = frame.time_seconds;

    // Convert to dB — prominence thresholding is intuitive in log scale
    std::vector<double> env_db(n_fb);
    for (size_t b = 0; b < n_fb; ++b) {
      env_db[b] = (frame.envelope[b] > kMinMag) ? 20.0 * std::log10(frame.envelope[b]) : -200.0;
    }

    // Find local maxima with topographic prominence ≥ kMinProminenceDB
    struct FramePeak {
      size_t bin;
      double amp_db;
    };
    std::vector<FramePeak> frame_peaks;

    for (size_t b = 1; b + 1 < n_fb; ++b) {
      if (env_db[b] < kMinAmplitudeDB) continue;
      if (env_db[b] <= env_db[b - 1] || env_db[b] <= env_db[b + 1]) continue;

      const double amp = env_db[b];

      // Scan left — minimum until a strictly higher bin is encountered
      double left_col = amp;
      for (size_t j = b; j > 0; --j) {
        if (env_db[j - 1] > amp) break;
        left_col = std::min(left_col, env_db[j - 1]);
      }
      // Scan right
      double right_col = amp;
      for (size_t j = b + 1; j < n_fb; ++j) {
        if (env_db[j] > amp) break;
        right_col = std::min(right_col, env_db[j]);
      }

      if (amp - std::max(left_col, right_col) >= kMinProminenceDB) frame_peaks.push_back({b, amp});
    }

    // Greedy track-peak matching by nearest frequency within kMaxFreqShiftHz
    struct Candidate {
      size_t track_i;
      size_t peak_j;
      double dist_hz;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(active.size() * frame_peaks.size());

    for (size_t ti = 0; ti < active.size(); ++ti) {
      for (size_t pj = 0; pj < frame_peaks.size(); ++pj) {
        const double freq_hz = static_cast<double>(frame_peaks[pj].bin) * freq_spacing;
        const double dist = std::abs(active[ti].last_freq_hz - freq_hz);
        if (dist <= kMaxFreqShiftHz) candidates.push_back({ti, pj, dist});
      }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
      return a.dist_hz < b.dist_hz;
    });

    std::vector<bool> track_matched(active.size(), false);
    std::vector<bool> peak_matched(frame_peaks.size(), false);

    for (const Candidate& c : candidates) {
      if (track_matched[c.track_i] || peak_matched[c.peak_j]) continue;
      track_matched[c.track_i] = true;
      peak_matched[c.peak_j] = true;

      TrackAccum& acc = active[c.track_i];
      const FramePeak& peak = frame_peaks[c.peak_j];
      const double freq_hz = static_cast<double>(peak.bin) * freq_spacing;

      acc.last_freq_hz = freq_hz;
      acc.times.push_back(t_frame);
      acc.freqs_hz.push_back(freq_hz);
      acc.amps_db.push_back(peak.amp_db);

      if (peak.amp_db > acc.peak_amp_db) {
        acc.peak_amp_db = peak.amp_db;
        acc.peak_frame = fi;
        acc.peak_bin = peak.bin;
      }
    }

    // Kill unmatched tracks, keep matched ones
    std::vector<TrackAccum> next_active;
    next_active.reserve(active.size());
    for (size_t ti = 0; ti < active.size(); ++ti) {
      if (!track_matched[ti])
        finalize(active[ti]);
      else
        next_active.push_back(std::move(active[ti]));
    }

    // Birth new tracks for unmatched peaks
    for (size_t pj = 0; pj < frame_peaks.size(); ++pj) {
      if (peak_matched[pj]) continue;
      const FramePeak& peak = frame_peaks[pj];
      const double freq_hz = static_cast<double>(peak.bin) * freq_spacing;

      TrackAccum acc;
      acc.birth_time_s = t_frame;
      acc.last_freq_hz = freq_hz;
      acc.peak_amp_db = peak.amp_db;
      acc.peak_frame = fi;
      acc.peak_bin = peak.bin;
      acc.times.push_back(t_frame);
      acc.freqs_hz.push_back(freq_hz);
      acc.amps_db.push_back(peak.amp_db);
      next_active.push_back(std::move(acc));
    }

    active = std::move(next_active);
    SFERIC_PROGRESS(fi, n_frames, t_frame * 1000.0, active.size() * fi * 3 * sizeof(double));
  }

  // Finalize tracks still alive at end of signal
  for (TrackAccum& acc : active) finalize(acc);

  SFERIC_LOG(Info, "tracks: " + std::to_string(completed.size()) + " completed");
  return completed;
}

}  // namespace analysis
}  // namespace sferic
