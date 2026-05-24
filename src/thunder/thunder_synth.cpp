#include "thunder/thunder_synth.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/constants.h"
#include "spatial/stereo.h"
#include "synthesis/noise_generator.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

// ── IIR filter helpers ───────────────────────────────────────────────────────

namespace {

// Apply a 2nd-order Butterworth lowpass at fc_hz in-place (Transposed Direct Form II).
// Below fc: gain ≈ 1.  Above fc: gain ≈ (fc/f)^2 — the f^{-2} rolloff predicted by
// Few (1969) for cylindrical blast waves.
void apply_butter2_lp(SampleBuffer& buf, double fc_hz, double sample_rate) {
  if (fc_hz <= 0.0 || buf.empty()) return;

  double w0 = TWO_PI * fc_hz / sample_rate;
  double sin_w0 = std::sin(w0);
  double cos_w0 = std::cos(w0);
  double alpha = sin_w0 / (2.0 * 0.7071067811865476);  // Q = 1/sqrt(2) Butterworth

  double b0 = (1.0 - cos_w0) / 2.0;
  double b1 =  1.0 - cos_w0;
  double b2 = (1.0 - cos_w0) / 2.0;
  double a0_inv = 1.0 / (1.0 + alpha);
  double a1 = -2.0 * cos_w0 * a0_inv;
  double a2 = (1.0 - alpha) * a0_inv;
  b0 *= a0_inv; b1 *= a0_inv; b2 *= a0_inv;

  double s1 = 0.0, s2 = 0.0;
  for (auto& samp : buf) {
    double x = static_cast<double>(samp);
    double y = b0 * x + s1;
    s1 = b1 * x - a1 * y + s2;
    s2 = b2 * x - a2 * y;
    samp = static_cast<float>(y);
  }
}

// Apply a 1st-order Butterworth highpass at fc_hz in-place.
void apply_butter1_hp(SampleBuffer& buf, double fc_hz, double sample_rate) {
  if (fc_hz <= 0.0 || buf.empty()) return;

  double w0 = TWO_PI * fc_hz / sample_rate;
  double K  = std::tan(w0 / 2.0);
  double a0 = 1.0 + K;
  double b0 =  1.0 / a0;
  double b1 = -b0;
  double a1 = (K - 1.0) / a0;

  double x_prev = 0.0, y_prev = 0.0;
  for (auto& samp : buf) {
    double x = static_cast<double>(samp);
    double y = b0 * x + b1 * x_prev - a1 * y_prev;
    x_prev = x; y_prev = y;
    samp = static_cast<float>(y);
  }
}

// Apply an RBJ low-shelf filter at shelf_hz in-place.
// Cuts (gain_db < 0) or boosts all energy below shelf_hz.
// Used to reduce infrasound/rumble content that the HP cannot reach without
// encroaching on the desired spectral peak.
void apply_low_shelf(SampleBuffer& buf, double shelf_hz, double gain_db,
                     double sample_rate) {
  if (shelf_hz <= 0.0 || buf.empty()) return;

  double A     = std::pow(10.0, gain_db / 40.0);
  double w0    = TWO_PI * shelf_hz / sample_rate;
  double cos_w = std::cos(w0);
  double sin_w = std::sin(w0);
  double alpha = sin_w / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0);

  double b0 =        A * ((A + 1.0) - (A - 1.0) * cos_w + 2.0 * std::sqrt(A) * alpha);
  double b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w);
  double b2 =        A * ((A + 1.0) - (A - 1.0) * cos_w - 2.0 * std::sqrt(A) * alpha);
  double a0 =             (A + 1.0) + (A - 1.0) * cos_w + 2.0 * std::sqrt(A) * alpha;
  double a1 = -2.0 *     ((A - 1.0) + (A + 1.0) * cos_w);
  double a2 =             (A + 1.0) + (A - 1.0) * cos_w - 2.0 * std::sqrt(A) * alpha;

  double a0_inv = 1.0 / a0;
  b0 *= a0_inv; b1 *= a0_inv; b2 *= a0_inv;
  a1 *= a0_inv; a2 *= a0_inv;

  double s1 = 0.0, s2 = 0.0;
  for (auto& samp : buf) {
    double x = static_cast<double>(samp);
    double y = b0 * x + s1;
    s1 = b1 * x - a1 * y + s2;
    s2 = b2 * x - a2 * y;
    samp = static_cast<float>(y);
  }
}

// Apply a cookbook (RBJ) peaking EQ at fc_hz in-place.
// Boosts (gain_db > 0) or cuts a band of width set by Q centred on fc.
// Used after the Butterworth bandpass to add a concentrated resonance —
// real lightning recordings consistently show a 6–14 dB peak above the
// broadband bandpass shape that filtered white noise cannot reproduce.
void apply_peaking_eq(SampleBuffer& buf, double fc_hz, double q,
                      double gain_db, double sample_rate) {
  if (fc_hz <= 0.0 || q <= 0.0 || buf.empty()) return;

  double A     = std::pow(10.0, gain_db / 40.0);
  double w0    = TWO_PI * fc_hz / sample_rate;
  double sin_w = std::sin(w0);
  double cos_w = std::cos(w0);
  double alpha = sin_w / (2.0 * q);

  double b0 = 1.0 + alpha * A;
  double b1 = -2.0 * cos_w;
  double b2 = 1.0 - alpha * A;
  double a0 = 1.0 + alpha / A;
  double a1 = -2.0 * cos_w;
  double a2 = 1.0 - alpha / A;

  double a0_inv = 1.0 / a0;
  b0 *= a0_inv; b1 *= a0_inv; b2 *= a0_inv;
  a1 *= a0_inv; a2 *= a0_inv;

  double s1 = 0.0, s2 = 0.0;
  for (auto& samp : buf) {
    double x = static_cast<double>(samp);
    double y = b0 * x + s1;
    s1 = b1 * x - a1 * y + s2;
    s2 = b2 * x - a2 * y;
    samp = static_cast<float>(y);
  }
}

// Apply a 2nd-order Butterworth highpass at fc_hz in-place (Transposed DF-II).
// Below fc: gain ≈ (f/fc)^2 (rising slope).  Above fc: gain ≈ 1 (flat).
// Pairing with the 2nd-order LP creates a broad spectral peak around fc_hp–fc_lp.
void apply_butter2_hp(SampleBuffer& buf, double fc_hz, double sample_rate) {
  if (fc_hz <= 0.0 || buf.empty()) return;

  double w0    = TWO_PI * fc_hz / sample_rate;
  double sin_w = std::sin(w0);
  double cos_w = std::cos(w0);
  double alpha = sin_w / (2.0 * 0.7071067811865476);  // Butterworth Q

  double b0 = (1.0 + cos_w) / 2.0;
  double b1 = -(1.0 + cos_w);
  double b2 = (1.0 + cos_w) / 2.0;
  double a0_inv = 1.0 / (1.0 + alpha);
  double a1 = -2.0 * cos_w * a0_inv;
  double a2 = (1.0 - alpha) * a0_inv;
  b0 *= a0_inv; b1 *= a0_inv; b2 *= a0_inv;

  double s1 = 0.0, s2 = 0.0;
  for (auto& samp : buf) {
    double x = static_cast<double>(samp);
    double y = b0 * x + s1;
    s1 = b1 * x - a1 * y + s2;
    s2 = b2 * x - a2 * y;
    samp = static_cast<float>(y);
  }
}

// Generate spectrally shaped thunder noise for one channel segment.
//
// Produces white noise shaped by a 2nd-order Butterworth bandpass pair:
//   HP at 0.5 × peak_hz — rising f^{+2} slope below the peak.
//   LP at 1.5 × peak_hz — falling f^{-2} slope above the peak.
//
// Together the HP and LP create a broad spectral peak centred near peak_hz.
// The LP cutoff is set 1.5× above (and HP cutoff 0.5× below) peak_hz so
// the knee frequencies bracket the target; the combined −3 dB point of the
// bandpass falls close to peak_hz.
//
// The f^{-2} LP rolloff matches Few (1969) cylindrical blast theory.
SampleBuffer make_thunder_noise(double peak_hz,
                                double duration_s,
                                double sample_rate,
                                uint32_t seed,
                                double hp_factor,
                                double lp_factor,
                                double resonance_q,
                                double resonance_gain_db,
                                double lf_shelf_hz,
                                double lf_shelf_gain_db) {
  size_t N = static_cast<size_t>(duration_s * sample_rate);
  if (N == 0) return {};

  std::mt19937 rng(seed);
  std::normal_distribution<float> dist(0.0f, 1.0f);

  SampleBuffer buf(N);
  for (auto& s : buf) s = dist(rng);

  apply_butter2_hp(buf, peak_hz * hp_factor, sample_rate);
  apply_butter2_lp(buf, peak_hz * lp_factor, sample_rate);
  if (resonance_q > 0.0 && resonance_gain_db != 0.0) {
    apply_peaking_eq(buf, peak_hz, resonance_q, resonance_gain_db, sample_rate);
  }
  if (lf_shelf_hz > 0.0 && lf_shelf_gain_db != 0.0) {
    apply_low_shelf(buf, lf_shelf_hz, lf_shelf_gain_db, sample_rate);
  }

  return buf;
}

// Apply tanh soft-saturation to the LF band (below corner_hz) and blend
// back at mix, adding sub-bass harmonics for perceived weight.
// drive_db sets the amount of overdrive before the tanh; 0 disables.
void apply_lf_saturation(SampleBuffer& buf, double corner_hz,
                          double drive_db, float mix, double sample_rate) {
  if (drive_db <= 0.0 || mix <= 0.0f || buf.empty()) return;

  SampleBuffer lf = buf;
  apply_butter2_lp(lf, corner_hz, sample_rate);

  double drive = std::pow(10.0, drive_db / 20.0);
  for (size_t i = 0; i < lf.size(); ++i) {
    lf[i] = static_cast<float>(std::tanh(static_cast<double>(lf[i]) * drive));
  }
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] = buf[i] * (1.0f - mix) + lf[i] * mix;
  }
}

// Inject stochastic tail crackles (Few 1969 "string of pearls" secondary
// arrivals).  Crackle onset times are drawn from a non-homogeneous Poisson
// process with exponentially decaying rate:
//   λ(t) = rate_hz × exp(−t / decay_tau_s)
// Each micro-crack is a short bandpass noise burst shaped by source_pulse.
void add_tail_crackles(SampleBuffer& output, double sample_rate,
                        double rate_hz, double decay_tau_s, float level,
                        double /*peak_hz*/, uint32_t seed) {
  if (rate_hz <= 0.0 || level <= 0.0f || output.empty()) return;

  double duration_s = static_cast<double>(output.size()) / sample_rate;
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  std::uniform_real_distribution<double> peak_dist(CRACKLE_PEAK_MIN_HZ,
                                                    CRACKLE_PEAK_MAX_HZ);
  std::uniform_real_distribution<double> dur_dist(CRACKLE_DUR_MIN_S,
                                                   CRACKLE_DUR_MAX_S);

  double t = 0.0;
  uint32_t crackle_seed = seed ^ 0x5EEDC0D3u;
  while (t < duration_s) {
    double lambda_t = rate_hz * std::exp(-t / decay_tau_s);
    if (lambda_t < 0.001) break;
    t += -std::log(std::max(uniform(rng), 1.0e-9)) / lambda_t;
    if (t >= duration_s) break;

    double c_peak_hz = peak_dist(rng);
    double c_dur_s   = dur_dist(rng);

    SampleBuffer crackle =
        make_thunder_noise(c_peak_hz, c_dur_s, sample_rate, ++crackle_seed,
                           0.5, 1.5, 1.0, 0.0, 0.0, 0.0);

    // Source-pulse amplitude envelope with t_p = 10 ms (micro-crack)
    constexpr double kMicroPulse = 0.010;
    double dt = 1.0 / sample_rate;
    for (size_t i = 0; i < crackle.size(); ++i) {
      double ti  = static_cast<double>(i) * dt;
      double env = std::abs(source_pulse(ti, kMicroPulse, 1.0));
      crackle[i] *= static_cast<float>(level * env);
    }

    size_t offset = static_cast<size_t>(t * sample_rate);
    for (size_t i = 0; i < crackle.size(); ++i) {
      if (offset + i < output.size()) {
        output[offset + i] += crackle[i];
      }
    }
  }
}

}  // namespace

// ── ThunderSynth helpers ─────────────────────────────────────────────────────

double ThunderSynth::estimate_pulse_duration(double current_ka) const {
  // Higher current → shorter pulse (sharper crack).
  // Linear scale from MAX at 0 kA to MIN at 200 kA.
  double t_ms = CRACK_POSITIVE_PULSE_MAX_MS -
                (CRACK_POSITIVE_PULSE_MAX_MS - CRACK_POSITIVE_PULSE_MIN_MS) *
                    std::clamp(current_ka / 200.0, 0.0, 1.0);
  return t_ms / 1000.0;
}

double ThunderSynth::auto_duration(const ThunderConfig& config) const {
  double segment_spread = config.lightning.segment_spread_s;
  if (segment_spread <= 0.0) {
    segment_spread = config.lightning.bolt_length_m / SPEED_OF_SOUND;
  }

  // segment_spread is the max arrival-time difference between segments (relative
  // delays). Add one segment duration (so the last segment decays fully) and
  // an environment tail for FDN and ground-reflection ring-out.
  constexpr double kSegDuration = 2.0;  // matches synthesize() constant
  constexpr double kEnvTail     = 1.5;  // empirical — no citation

  return segment_spread + kSegDuration + kEnvTail;
}

analysis::StochasticModel ThunderSynth::build_thunder_stochastic(
    double peak_hz, double duration_s, double sample_rate) const {
  analysis::StochasticModel model;
  model.sample_rate = sample_rate;
  model.num_envelope_points = CRACK_ENVELOPE_POINTS;

  double nyquist = sample_rate / 2.0;
  double freq_spacing = nyquist / static_cast<double>(CRACK_ENVELOPE_POINTS - 1);

  // Time frames at ~10 ms intervals
  double frame_interval = 0.01;
  size_t num_frames = std::max(size_t{2},
                               static_cast<size_t>(duration_s / frame_interval) + 1);

  // Pure spectral shape — no temporal decay here.
  // Temporal shaping (source pulse envelope) is applied in the time domain.
  constexpr double kShelfFreq = 8.0;  // Hz — infrasound shelf

  for (size_t f = 0; f < num_frames; ++f) {
    double t = static_cast<double>(f) * frame_interval;

    analysis::StochasticFrame frame;
    frame.time_seconds = t;
    frame.frequency_spacing = freq_spacing;
    frame.envelope.resize(CRACK_ENVELOPE_POINTS);

    for (size_t j = 0; j < CRACK_ENVELOPE_POINTS; ++j) {
      double freq = static_cast<double>(j) * freq_spacing;
      double magnitude = 0.0;

      if (freq < kShelfFreq) {
        magnitude = 0.5;
      } else if (freq <= peak_hz) {
        double frac = (freq - kShelfFreq) / (peak_hz - kShelfFreq);
        magnitude = 0.5 + 0.5 * frac;
      } else {
        magnitude = (peak_hz / freq) * (peak_hz / freq);
      }

      frame.envelope[j] = magnitude;
    }

    model.frames.push_back(frame);
  }

  return model;
}

PropagationConfig propagation_for_segment(const PropagationConfig& base,
                                          const LightningSegment& segment) {
  PropagationConfig cfg = base;
  cfg.distance_m = segment.slant_distance_m;
  return cfg;
}

// ── Main synthesis ───────────────────────────────────────────────────────────

SampleBuffer ThunderSynth::synthesize(const ThunderConfig& config,
                                      double sample_rate) const {
  // 1. Determine duration
  double duration_s = config.duration_s;
  if (duration_s <= 0.0) {
    duration_s = auto_duration(config);
  }

  // 2. Build channel segments
  auto segments = make_channel(config.lightning);
  if (segments.empty()) return {};

  // 3. Extend for pre-crack stepped-leader precursor and allocate output.
  const double pre_crack_shift =
      config.pre_crack_enabled ? config.pre_crack_offset_s : 0.0;
  size_t total_samples =
      static_cast<size_t>((duration_s + pre_crack_shift) * sample_rate);
  SampleBuffer output(total_samples, 0.0f);

  if (config.pre_crack_enabled) {
    // Brief high-frequency burst injected at t = 0; main thunder starts at
    // pre_crack_shift seconds.  Empirical shape — no published measurement
    // of the stepped-leader acoustic spectral peak was found.
    constexpr double kPreCrackDur = 0.10;  // 100 ms — empirical
    constexpr double kPreDecay    = 0.04;  // 40 ms decay — empirical
    SampleBuffer burst =
        make_thunder_noise(config.pre_crack_freq_hz, kPreCrackDur, sample_rate,
                           config.lightning.rng_seed ^ 0xBEEF5EEDu,
                           0.5, 1.5, 1.5, 6.0, 0.0, 0.0);
    double dt = 1.0 / sample_rate;
    for (size_t i = 0; i < burst.size(); ++i) {
      double t = static_cast<double>(i) * dt;
      burst[i] *= static_cast<float>(config.pre_crack_level *
                                     std::exp(-t / kPreDecay));
    }
    for (size_t i = 0; i < burst.size() && i < total_samples; ++i) {
      output[i] += burst[i];
    }
  }

  // 4. Find closest segment (highest amplitude_scale)
  size_t closest_idx = 0;
  double max_amp = 0.0;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].amplitude_scale > max_amp) {
      max_amp = segments[i].amplitude_scale;
      closest_idx = i;
    }
  }

  // 5. Source amplitude from physics
  double p_max = peak_overpressure_pa(config.lightning.current_ka);

  // Only used for the crack_profile path
  synthesis::NoiseGenerator noise_gen(sample_rate);

  for (size_t seg_idx = 0; seg_idx < segments.size(); ++seg_idx) {
    const auto& seg = segments[seg_idx];

    // Segment duration — long enough that adjacent segments (spaced
    // bolt_length/(n-1)/v_air ≈ 580 ms apart) overlap at ~−10 dB.
    // With decay_tau = 500 ms, at t = 580 ms the envelope is at 31 %
    // (−10 dB), giving continuous overlapping rumble.
    constexpr double kSegDuration = 2.0;  // empirical — no citation
    double seg_duration = kSegDuration;
    size_t seg_samples  = static_cast<size_t>(seg_duration * sample_rate);
    if (seg_samples == 0) continue;

    // ── Spectral shaping ─────────────────────────────────────────────────────
    // For the closest segment when a measured crack profile is provided,
    // use the stochastic model (better match to recording).
    // For all other segments, use the direct IIR filter path — which
    // correctly places the spectral peak at peak_frequency_hz and gives
    // the f^{-2} rolloff predicted by Few (1969).  The stochastic model
    // path cannot be used here because CRACK_ENVELOPE_POINTS = 64 gives
    // 350 Hz / bin, collapsing the entire 0–300 Hz thunder band into one
    // envelope point and destroying all spectral structure.
    SampleBuffer shaped;

    if (seg_idx == closest_idx && config.crack_profile.has_value()) {
      auto seg_model = config.crack_profile.value();
      noise_gen.load_model(seg_model);
      shaped.resize(seg_samples, 0.0f);
      noise_gen.render(shaped.data(), seg_samples, 0.0);
    } else {
      shaped = make_thunder_noise(
          seg.peak_frequency_hz, seg_duration, sample_rate,
          config.lightning.rng_seed + static_cast<uint32_t>(seg_idx),
          config.lightning.hp_cutoff_factor,
          config.lightning.lp_cutoff_factor,
          config.lightning.resonance_q,
          config.lightning.resonance_gain_db,
          config.lightning.lf_shelf_hz,
          config.lightning.lf_shelf_gain_db);
    }

    // Temporal envelope:
    // Crack (closest segment): physical source-pulse shape from Few (1969) —
    //   same envelope used by generate_segment_burst().
    // Rumble (all other segments): slow 2 s exponential decay so adjacent
    //   arrivals overlap fully and produce continuous rolling thunder.
    //   Empirical — no citation.
    {
      double dt = 1.0 / sample_rate;
      if (seg_idx == closest_idx) {
        double t_p_s = estimate_pulse_duration(config.lightning.current_ka);
        for (size_t i = 0; i < shaped.size(); ++i) {
          double t   = static_cast<double>(i) * dt;
          shaped[i] *= static_cast<float>(std::abs(source_pulse(t, t_p_s, 1.0)));
        }
      } else {
        constexpr double decay_tau = 2.0;
        for (size_t i = 0; i < shaped.size(); ++i) {
          double t   = static_cast<double>(i) * dt;
          shaped[i] *= static_cast<float>(std::exp(-t / decay_tau));
        }
      }
    }

    // Apply propagation for this segment's distance
    PropagationConfig seg_prop = propagation_for_segment(config.propagation, seg);
    PropagationFilter filter(seg_prop, sample_rate, 512);
    filter.apply(shaped);

    // Scale by amplitude and overpressure
    double scale = seg.amplitude_scale * p_max;
    // Normalise overpressure to audio range (p_max is in Pa; divide by
    // reference ~100 Pa so typical thunder doesn't clip before peak normalisation)
    scale /= 100.0;

    // Sum into output at correct delay (shifted forward when pre-crack enabled)
    size_t delay_offset =
        static_cast<size_t>((seg.delay_s + pre_crack_shift) * sample_rate);
    for (size_t i = 0; i < shaped.size(); ++i) {
      size_t out_idx = delay_offset + i;
      if (out_idx < total_samples) {
        output[out_idx] += static_cast<Sample>(scale) * shaped[i];
      }
    }
  }

  // 7. Apply environment
  EnvironmentProcessor env(config.environment, sample_rate);
  env.apply(output);

  // 8. Stochastic tail crackles — Few (1969) "string of pearls" secondary arrivals.
  if (config.tail_crackles_enabled) {
    float crackle_level =
        static_cast<float>(segments[closest_idx].amplitude_scale) * 0.3f;
    add_tail_crackles(output, sample_rate, config.crackle_rate_hz,
                      config.crackle_decay_tau_s, crackle_level,
                      segments[closest_idx].peak_frequency_hz,
                      config.lightning.rng_seed ^ 0xC4AC4E7Au);
  }

  // 9. LF harmonic saturation — tanh soft-clip on sub-bass band.
  apply_lf_saturation(output, config.lf_saturation_hz,
                       config.lf_saturation_drive_db,
                       config.lf_saturation_mix, sample_rate);

  // 10. Peak-normalise to target level.
  // RMS normalisation is inappropriate here: 16 short bursts spread over
  // ~12 s give a duty cycle of ~10 %, so full-buffer RMS is much lower than
  // active-region RMS.  That would require a large gain that pushes peaks
  // well above 0 dBFS.  Peak normalisation avoids clipping regardless of
  // duty cycle while preserving the relative loudness of the crack vs rumble.
  float peak_val = 0.0f;
  for (size_t i = 0; i < total_samples; ++i) {
    if (std::abs(output[i]) > peak_val) peak_val = std::abs(output[i]);
  }
  if (peak_val > 1.0e-10f) {
    // target_rms repurposed as target_peak in (0,1] for this path
    float gain = static_cast<float>(config.target_rms) / peak_val;
    for (size_t i = 0; i < total_samples; ++i) {
      output[i] *= gain;
    }
  }

  return output;
}

// ── Stereo synthesis ─────────────────────────────────────────────────────────

AudioBuffer ThunderSynth::synthesize_stereo(const ThunderConfig& config,
                                            double sample_rate) const {
  double duration_s = config.duration_s;
  if (duration_s <= 0.0) {
    duration_s = auto_duration(config);
  }

  auto segments = make_channel(config.lightning);
  if (segments.empty()) return {};

  const double pre_crack_shift =
      config.pre_crack_enabled ? config.pre_crack_offset_s : 0.0;
  size_t total_samples =
      static_cast<size_t>((duration_s + pre_crack_shift) * sample_rate);
  AudioBuffer output(2, total_samples, sample_rate);

  if (config.pre_crack_enabled) {
    constexpr double kPreCrackDur = 0.10;
    constexpr double kPreDecay    = 0.04;
    SampleBuffer burst =
        make_thunder_noise(config.pre_crack_freq_hz, kPreCrackDur, sample_rate,
                           config.lightning.rng_seed ^ 0xBEEF5EEDu,
                           0.5, 1.5, 1.5, 6.0, 0.0, 0.0);
    double dt = 1.0 / sample_rate;
    for (size_t i = 0; i < burst.size(); ++i) {
      double t = static_cast<double>(i) * dt;
      float s = burst[i] * static_cast<float>(config.pre_crack_level *
                                               std::exp(-t / kPreDecay));
      if (i < total_samples) {
        output.at(0, i) += s;
        output.at(1, i) += s;
      }
    }
  }

  size_t closest_idx = 0;
  double max_amp = 0.0;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].amplitude_scale > max_amp) {
      max_amp = segments[i].amplitude_scale;
      closest_idx = i;
    }
  }

  double p_max = peak_overpressure_pa(config.lightning.current_ka);
  synthesis::NoiseGenerator noise_gen(sample_rate);
  spatial::StereoProcessor stereo(config.stereo);

  for (size_t seg_idx = 0; seg_idx < segments.size(); ++seg_idx) {
    const auto& seg = segments[seg_idx];

    constexpr double kSegDuration = 2.0;
    const double seg_duration = kSegDuration;
    size_t seg_samples = static_cast<size_t>(seg_duration * sample_rate);
    if (seg_samples == 0) continue;

    SampleBuffer shaped;
    if (seg_idx == closest_idx && config.crack_profile.has_value()) {
      auto seg_model = config.crack_profile.value();
      noise_gen.load_model(seg_model);
      shaped.resize(seg_samples, 0.0f);
      noise_gen.render(shaped.data(), seg_samples, 0.0);
    } else {
      shaped = make_thunder_noise(
          seg.peak_frequency_hz, seg_duration, sample_rate,
          config.lightning.rng_seed + static_cast<uint32_t>(seg_idx),
          config.lightning.hp_cutoff_factor,
          config.lightning.lp_cutoff_factor,
          config.lightning.resonance_q,
          config.lightning.resonance_gain_db,
          config.lightning.lf_shelf_hz,
          config.lightning.lf_shelf_gain_db);
    }

    {
      double dt = 1.0 / sample_rate;
      if (seg_idx == closest_idx) {
        double t_p_s = estimate_pulse_duration(config.lightning.current_ka);
        for (size_t i = 0; i < shaped.size(); ++i) {
          double t   = static_cast<double>(i) * dt;
          shaped[i] *= static_cast<float>(std::abs(source_pulse(t, t_p_s, 1.0)));
        }
      } else {
        constexpr double decay_tau = 2.0;
        for (size_t i = 0; i < shaped.size(); ++i) {
          double t   = static_cast<double>(i) * dt;
          shaped[i] *= static_cast<float>(std::exp(-t / decay_tau));
        }
      }
    }

    PropagationConfig seg_prop = propagation_for_segment(config.propagation, seg);
    PropagationFilter filter(seg_prop, sample_rate, 512);
    filter.apply(shaped);

    double scale = seg.amplitude_scale * p_max / 100.0;

    spatial::Position pos{seg.position_x_m + config.strike_offset_x_m,
                          seg.position_y_m + config.strike_offset_y_m,
                          seg.position_z_m};

    size_t delay_offset =
        static_cast<size_t>((seg.delay_s + pre_crack_shift) * sample_rate);
    if (delay_offset >= total_samples) continue;

    if (seg_idx == closest_idx) {
      // Crack: single shared source — physically correct stereo via render_into.
      for (auto& s : shaped) s *= static_cast<float>(scale);
      stereo.render_into(shaped, pos, output, delay_offset);
    } else {
      // Rumble: generate an independent secondary noise buffer for the right
      // channel so L and R contain genuinely different waveforms.
      uint32_t sec_seed =
          (config.secondary_tail_seed != 0)
              ? config.secondary_tail_seed +
                    static_cast<uint32_t>(seg_idx)
              : (config.lightning.rng_seed ^ 0xA5A5A5A5u) +
                    static_cast<uint32_t>(seg_idx);

      SampleBuffer shaped_r = make_thunder_noise(
          seg.peak_frequency_hz, seg_duration, sample_rate, sec_seed,
          config.lightning.hp_cutoff_factor, config.lightning.lp_cutoff_factor,
          config.lightning.resonance_q, config.lightning.resonance_gain_db,
          config.lightning.lf_shelf_hz, config.lightning.lf_shelf_gain_db);

      {
        constexpr double decay_tau = 2.0;
        double dt = 1.0 / sample_rate;
        for (size_t i = 0; i < shaped_r.size(); ++i) {
          double t    = static_cast<double>(i) * dt;
          shaped_r[i] *= static_cast<float>(std::exp(-t / decay_tau));
        }
      }

      PropagationFilter filter_r(seg_prop, sample_rate, 512);
      filter_r.apply(shaped_r);

      // Apply per-ear delays and gains from binaural geometry.
      auto frame = stereo.compute_frame(pos);
      size_t l_delay =
          delay_offset +
          static_cast<size_t>(frame.left_delay_s * sample_rate);
      size_t r_delay =
          delay_offset +
          static_cast<size_t>(frame.right_delay_s * sample_rate);

      for (size_t i = 0; i < shaped.size(); ++i) {
        if (l_delay + i < total_samples) {
          output.at(0, l_delay + i) +=
              shaped[i] * static_cast<float>(scale * frame.left_gain);
        }
      }
      for (size_t i = 0; i < shaped_r.size(); ++i) {
        if (r_delay + i < total_samples) {
          output.at(1, r_delay + i) +=
              shaped_r[i] * static_cast<float>(scale * frame.right_gain);
        }
      }
    }
  }

  // Apply environment per channel (independent reverb / FDN tails per ear).
  EnvironmentProcessor env(config.environment, sample_rate);
  SampleBuffer left(total_samples);
  SampleBuffer right(total_samples);
  for (size_t i = 0; i < total_samples; ++i) {
    left[i] = output.at(0, i);
    right[i] = output.at(1, i);
  }
  env.apply(left);
  env.apply(right);

  // Stochastic tail crackles — independent per channel for decorrelated stereo.
  if (config.tail_crackles_enabled) {
    float crackle_level =
        static_cast<float>(segments[closest_idx].amplitude_scale) * 0.3f;
    double crack_peak_hz = segments[closest_idx].peak_frequency_hz;
    add_tail_crackles(left, sample_rate, config.crackle_rate_hz,
                      config.crackle_decay_tau_s, crackle_level, crack_peak_hz,
                      config.lightning.rng_seed ^ 0xC4AC4E7Au);
    add_tail_crackles(right, sample_rate, config.crackle_rate_hz,
                      config.crackle_decay_tau_s, crackle_level, crack_peak_hz,
                      config.lightning.rng_seed ^ 0x3B5CA91Fu);
  }

  // LF harmonic saturation per channel.
  apply_lf_saturation(left,  config.lf_saturation_hz,
                       config.lf_saturation_drive_db,
                       config.lf_saturation_mix, sample_rate);
  apply_lf_saturation(right, config.lf_saturation_hz,
                       config.lf_saturation_drive_db,
                       config.lf_saturation_mix, sample_rate);

  for (size_t i = 0; i < total_samples; ++i) {
    output.at(0, i) = left[i];
    output.at(1, i) = right[i];
  }

  // Joint peak-normalisation across both channels so the inter-channel
  // balance is preserved.
  float peak_val = 0.0f;
  for (size_t i = 0; i < total_samples; ++i) {
    peak_val = std::max(peak_val, std::abs(output.at(0, i)));
    peak_val = std::max(peak_val, std::abs(output.at(1, i)));
  }
  if (peak_val > 1.0e-10f) {
    float gain = static_cast<float>(config.target_rms) / peak_val;
    for (size_t i = 0; i < total_samples; ++i) {
      output.at(0, i) *= gain;
      output.at(1, i) *= gain;
    }
  }

  return output;
}

}  // namespace thunder
}  // namespace sferic
