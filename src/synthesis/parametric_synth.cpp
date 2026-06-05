#include "synthesis/parametric_synth.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "core/logger.h"
#include "synthesis/noise_generator.h"

namespace sferic {
namespace synthesis {

ParametricSynth::ParametricSynth() = default;

void ParametricSynth::load_model(const analysis::ParametricModel& model) {
  SFERIC_SCOPE("load_model");
  model_ = model;
  std::ostringstream ss;
  ss << "loaded parametric model: " << model_.source_frame_count << " source frames"
     << "  duration=" << model_.duration_s << "s"
     << "  bands=" << model_.bands.size();
  SFERIC_LOG(Info, ss.str());
}

void ParametricSynth::clear() { model_ = {}; }

AudioBuffer ParametricSynth::synthesize(std::optional<double> duration_s) const {
  SFERIC_SCOPE("synthesize");

  const double dur = duration_s.value_or(model_.duration_s);

  const analysis::SpectralEnvelope envelope = reconstruct_envelope(dur);

  NoiseGenerator gen;
  gen.load_model(envelope);

  const size_t num_samples = static_cast<size_t>(dur * model_.sample_rate);
  AudioBuffer out(1, num_samples, model_.sample_rate);
  gen.render(out.channel(0), num_samples, 0.0);

  return out;
}

void ParametricSynth::prepare_stream(double duration_s) {
  SFERIC_SCOPE("prepare_stream");
  stream_total_frames_ = static_cast<size_t>(std::round(duration_s * model_.sample_rate));
  stream_pos_frames_ = 0;
  // TODO impl — initialise per-band AR(1) residual state + NoiseGenerator grains
  // so render_block can synthesize lazily without reconstruct_envelope().
}

size_t ParametricSynth::render_block(float* out, size_t num_samples) {
  // TODO impl — synthesize the next num_samples lazily. Stub emits silence and
  // advances the stream clock so the realtime wiring can be exercised end-to-end.
  const size_t remaining =
      (stream_pos_frames_ < stream_total_frames_) ? stream_total_frames_ - stream_pos_frames_ : 0;
  const size_t n = std::min(num_samples, remaining);
  for (size_t i = 0; i < n; ++i) out[i] = 0.0f;
  stream_pos_frames_ += n;
  return n;
}

// Linear interpolation into a trajectory vector at fractional position alpha in [0,1].
static double interp_trajectory(const std::vector<double>& traj, double alpha) {
  const double src = alpha * static_cast<double>(traj.size() - 1);
  const size_t lo = static_cast<size_t>(src);
  const size_t hi = std::min(lo + 1, traj.size() - 1);
  return std::lerp(traj[lo], traj[hi], src - static_cast<double>(lo));
}

// Evaluate a PiecewiseLinear macro contour at alpha in [0,1].
// weights are interleaved [t₀,v₀, t₁,v₁, ...] with t ascending in [0,1];
// returns the peak-normalised value (caller multiplies by the absolute scale).
static double eval_piecewise_linear(const std::vector<double>& w, double alpha) {
  const size_t k = w.size() / 2;
  if (k == 0) return 0.0;
  if (alpha <= w[0]) return w[1];
  if (alpha >= w[2 * (k - 1)]) return w[2 * (k - 1) + 1];
  for (size_t i = 0; i + 1 < k; ++i) {
    const double t0 = w[2 * i], t1 = w[2 * (i + 1)];
    if (alpha >= t0 && alpha <= t1) {
      const double f = (t1 > t0) ? (alpha - t0) / (t1 - t0) : 0.0;
      return std::lerp(w[2 * i + 1], w[2 * (i + 1) + 1], f);
    }
  }
  return w[2 * (k - 1) + 1];
}

// Analytic mean of a fitted distribution — the AR(1) centre for residual texture.
// Non-exhaustive by design: kinds fit_distribution never emits fall to the neutral
// multiplier 1.0 (sample_count == 0 means no fit was accepted).
static double distribution_mean(const analysis::DistributionFit& d) {
  using K = analysis::DistributionKind;
  if (d.sample_count == 0) return 1.0;
  switch (d.kind) {
    case K::Normal:
      return d.params[0];
    case K::Lognormal:
      return std::exp(d.params[0] + 0.5 * d.params[1] * d.params[1]);
    case K::Gamma:
      return d.params[0] * d.params[1];
    case K::Weibull:
      return d.params[1] * std::tgamma(1.0 + 1.0 / d.params[0]);
    case K::Exponential:
      return 1.0 / d.params[0];
    case K::Cauchy:
      return d.params[0];  // median — mean undefined
    default:
      return 1.0;
  }
}

// Draw one sample from a fitted distribution using the matching <random> engine.
static double sample_distribution(const analysis::DistributionFit& d, std::mt19937_64& rng) {
  using K = analysis::DistributionKind;
  if (d.sample_count == 0) return 1.0;
  switch (d.kind) {
    case K::Normal:
      return std::normal_distribution<double>(d.params[0], d.params[1])(rng);
    case K::Lognormal:
      return std::lognormal_distribution<double>(d.params[0], d.params[1])(rng);
    case K::Gamma:
      return std::gamma_distribution<double>(d.params[0], d.params[1])(rng);
    case K::Weibull:
      return std::weibull_distribution<double>(d.params[0], d.params[1])(rng);
    case K::Exponential:
      return std::exponential_distribution<double>(d.params[0])(rng);
    case K::Cauchy:
      return std::cauchy_distribution<double>(d.params[0], d.params[1])(rng);
    default:
      return 1.0;
  }
}

std::vector<double> ParametricSynth::amplitude_series(const analysis::TemporalEnvelope& env,
                                                      size_t num_frames,
                                                      std::mt19937_64& rng) const {
  std::vector<double> out(num_frames, 0.0);
  if (num_frames == 0) return out;

  // When the per-frame trajectory is available (full model, not compact), use it
  // directly. It captures the actual temporal dynamics — transients, variance bursts,
  // crack events — that the PiecewiseLinear macro smooths over. The trajectory IS
  // the envelope; the parametric path below exists only for compact mode.
  if (!env.rms_trajectory.empty()) {
    for (size_t i = 0; i < num_frames; ++i) {
      const double alpha =
          num_frames > 1 ? static_cast<double>(i) / static_cast<double>(num_frames - 1) : 0.0;
      out[i] = interp_trajectory(env.rms_trajectory, alpha);
    }
    return out;
  }

  // Compact mode — no trajectory stored. Use PiecewiseLinear macro contour ×
  // AR(1)-filtered residual:  y[i] = μ + φ(y[i-1]−μ) + √(1−φ²)(s[i]−μ)
  const auto& macro = env.macro_shape;
  const bool have_macro =
      macro.kind == analysis::DistributionKind::PiecewiseLinear && macro.weights.size() >= 2;
  if (!have_macro) return out;

  const double scale = macro.params[0];
  const double mu = distribution_mean(env.residual_distribution);
  const double phi = std::clamp(env.residual_autocorr, 0.0, 0.999);
  const double drive = std::sqrt(1.0 - phi * phi);

  double y = mu;
  for (size_t i = 0; i < num_frames; ++i) {
    const double alpha =
        num_frames > 1 ? static_cast<double>(i) / static_cast<double>(num_frames - 1) : 0.0;
    const double contour = eval_piecewise_linear(macro.weights, alpha) * scale;
    double s = sample_distribution(env.residual_distribution, rng);
    if (!std::isfinite(s)) s = mu;
    y = mu + phi * (y - mu) + drive * (s - mu);
    out[i] = std::max(0.0, contour * y);
  }
  return out;
}

analysis::SpectralEnvelope ParametricSynth::reconstruct_envelope(double duration_s) const {
  SFERIC_SCOPE("reconstruct_envelope");

  // Bin count and frequency spacing come from the model — no coupling to the call site.
  const size_t num_bins = model_.num_bins;
  const double freq_spacing = model_.sample_rate / static_cast<double>((num_bins - 1) * 2);
  const size_t num_frames = static_cast<size_t>(std::round(duration_s * 1000.0));

  const bool use_bands = !model_.bands.empty();
  {
    std::ostringstream ss;
    ss << "num_bins=" << num_bins << "  num_frames=" << num_frames << "  duration=" << duration_s
       << "s"
       << "  bands=" << model_.bands.size()
       << (use_bands ? " (per-band synthesis)" : " (global shape fallback)");
    SFERIC_LOG(Info, ss.str());
  }

  analysis::SpectralEnvelope envelope;
  envelope.sample_rate = model_.sample_rate;
  envelope.ms.reserve(num_frames);

  const auto& traj = model_.spectral_trajectory;
  size_t accumulated_bytes = 0;

  // Pre-generate amplitude (RMS) series purely from parameters — macro contour ×
  // AR(1)-filtered residual. No recorded rms_trajectory array is read here; this
  // is what makes the synth parametric rather than a sample replayer.
  // Fixed seed → deterministic output for tests and A/B comparison.
  std::mt19937_64 rng(0x5FE71C5CA9Eull);
  std::vector<std::vector<double>> band_amp_series;
  std::vector<double> overall_series;
  if (use_bands) {
    band_amp_series.reserve(model_.bands.size());
    for (const auto& band : model_.bands)
      band_amp_series.push_back(amplitude_series(band.envelope, num_frames, rng));
  } else {
    overall_series = amplitude_series(model_.overall_envelope, num_frames, rng);
  }

  for (size_t i = 0; i < num_frames; ++i) {
    const double t = static_cast<double>(i) / 1000.0;
    const double alpha =
        num_frames > 1 ? static_cast<double>(i) / static_cast<double>(num_frames - 1) : 0.0;

    analysis::EnvelopeFrame frame;
    frame.time_seconds = t;
    frame.frequency_spacing = freq_spacing;
    frame.envelope.assign(num_bins, 0.0);

    if (use_bands) {
      // Per-band synthesis — each band contributes to its own frequency range using
      // its parametric amplitude series and its spectral shape computed within that range.
      // This preserves the inter-band energy ratio and the correct spectral character
      // of each frequency region (e.g. low-frequency boom vs high-frequency crack).
      for (size_t bi = 0; bi < model_.bands.size(); ++bi) {
        const double band_amp = band_amp_series[bi][i];
        if (band_amp <= 0.0) continue;

        const auto& band = model_.bands[bi];
        const size_t b_lo = std::min(band.bin_lo, num_bins - 1);
        const size_t b_hi = std::min(band.bin_hi, num_bins - 1);

        auto band_env =
            envelope_from_shape(band.mean_shape, band_amp, num_bins, freq_spacing, b_lo, b_hi);
        for (size_t b = b_lo; b <= b_hi; ++b) frame.envelope[b] += band_env[b];
      }

      // Smooth band boundary discontinuities — adjacent bands independently
      // L2-normalise their shapes, so the shared boundary bin can have an abrupt
      // step between the two contributions. A [¼ ½ ¼] kernel removes 1-bin steps
      // without blurring features wider than 3 bins (narrowest band is 7 bins).
      {
        std::vector<double> smoothed(num_bins);
        smoothed[0] = frame.envelope[0];
        for (size_t b = 1; b + 1 < num_bins; ++b)
          smoothed[b] = 0.25 * frame.envelope[b - 1] + 0.50 * frame.envelope[b] +
                        0.25 * frame.envelope[b + 1];
        smoothed[num_bins - 1] = frame.envelope[num_bins - 1];
        frame.envelope = std::move(smoothed);
      }
    } else {
      // Fallback: global shape when no bands were detected.
      // Interpolate the per-frame spectral trajectory for time-varying shape.
      const double amplitude = overall_series[i];

      const double src = alpha * static_cast<double>(traj.size() - 1);
      const size_t lo = static_cast<size_t>(src);
      const size_t hi = std::min(lo + 1, traj.size() - 1);
      const double f = src - static_cast<double>(lo);
      const auto& sa = traj[lo];
      const auto& sb = traj[hi];
      analysis::SpectralShape shape = model_.mean_spectral_shape;
      shape.centroid_hz = std::lerp(sa.centroid_hz, sb.centroid_hz, f);
      shape.spread_hz = std::lerp(sa.spread_hz, sb.spread_hz, f);
      shape.tilt_db_octave = std::lerp(sa.tilt_db_octave, sb.tilt_db_octave, f);
      shape.flatness = std::lerp(sa.flatness, sb.flatness, f);
      shape.rolloff_hz = std::lerp(sa.rolloff_hz, sb.rolloff_hz, f);
      frame.envelope = envelope_from_shape(shape, amplitude, num_bins, freq_spacing);
    }

    accumulated_bytes += num_bins * sizeof(double);
    envelope.ms.push_back(std::move(frame));

    SFERIC_PROGRESS(i, num_frames, t * 1000.0, accumulated_bytes);
  }

  SFERIC_LOG_MEM(Info, "reconstruct_envelope complete", accumulated_bytes);
  return envelope;
}

std::vector<double> ParametricSynth::envelope_from_shape(const analysis::SpectralShape& shape,
                                                         double amplitude, size_t num_bins,
                                                         double freq_spacing, size_t bin_lo,
                                                         size_t bin_hi) const {
  std::vector<double> env(num_bins, 0.0);
  if (amplitude <= 0.0) return env;

  // Clamp range to valid bins
  bin_lo = std::min(bin_lo, num_bins - 1);
  bin_hi = std::min(bin_hi, num_bins - 1);
  const size_t n_active = bin_hi - bin_lo + 1;

  const double centroid = std::max(shape.centroid_hz, freq_spacing);
  const double spread = std::max(shape.spread_hz, freq_spacing);
  const double flatness = std::clamp(shape.flatness, 0.0, 1.0);

  for (size_t b = bin_lo; b <= bin_hi; ++b) {
    const double f = std::max(static_cast<double>(b) * freq_spacing, freq_spacing);

    // Tilt: log-linear slope in dB, anchored at centroid.
    // Clamped to ±40 dB — prevents unbounded extrapolation far from centroid
    // (e.g. a steep negative tilt would otherwise diverge toward DC).
    const double tilt_db =
        std::clamp(shape.tilt_db_octave * std::log2(f / centroid), -40.0, 40.0);  // empirical
    const double tilt_linear = std::pow(10.0, tilt_db / 20.0);

    // Gaussian concentration around centroid — in log-frequency space so that the
    // same spread_hz parameter works for both narrow tonal bands and wideband noise
    // bands. A linear-frequency Gaussian would create an asymmetric bell in octave
    // space and severely concentrate energy near the centroid for wide bands.
    // sigma_oct converts spread_hz to an equivalent octave width at the centroid.
    const double sigma_oct = std::log2((centroid + spread) / centroid);
    const double gauss = std::exp(-0.5 * std::pow(std::log2(f / centroid) / sigma_oct, 2.0));

    // flatness=1 → shaped by tilt alone (white/pink noise)
    // flatness=0 → Gaussian-concentrated at centroid (tonal)
    // rolloff_hz is a descriptive percentile (85% energy), not a filter parameter —
    // the tilt already captures spectral decay; applying a separate rolloff filter
    // would double-suppress frequencies above rolloff_hz.
    env[b] = std::lerp(gauss * tilt_linear, tilt_linear, flatness);
  }

  // Scale L2 norm within the active bin range to match the normalized STFT envelope
  // convention. The analysis stage divides magnitudes by window_sum/2 so a sine of
  // amplitude A produces ~A. Expected L2 over n_active bins = sqrt(n_active) * amplitude.
  // For the full-spectrum case this is sqrt(num_bins)*amplitude (empirically verified).
  const double target_l2 = std::sqrt(static_cast<double>(n_active)) * amplitude;
  double l2 = 0.0;
  for (size_t b = bin_lo; b <= bin_hi; ++b) l2 += env[b] * env[b];
  l2 = std::sqrt(l2);
  if (l2 > 0.0) {
    const double scale = target_l2 / l2;
    for (size_t b = bin_lo; b <= bin_hi; ++b) env[b] *= scale;
  }

  return env;
}

}  // namespace synthesis
}  // namespace sferic
