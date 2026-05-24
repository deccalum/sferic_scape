#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "analysis/stochastic_model.h"
#include "core/buffer.h"
#include "core/types.h"
#include "spatial/stereo.h"
#include "thunder/crack_analyzer.h"
#include "thunder/environment_model.h"
#include "thunder/lightning_model.h"
#include "thunder/propagation_model.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

/** @brief Unified configuration for a single thunder synthesis call.
 *
 *  @details Combines all three physical layers (source, propagation, environment)
 *  and an optional crack profile extracted from a real recording.
 *
 *  Pipeline executed by ThunderSynth::synthesize():
 *  1. LightningModel → segment list (delays, distances, amplitude scales)
 *  2. Per segment:
 *     - generate_segment_burst() → noise burst shaped to source pulse
 *     - build_thunder_stochastic() → thunder spectral envelope (~65 Hz at source)
 *     - NoiseGenerator::render() → spectrally shaped segment audio
 *     - PropagationFilter::apply() → distance attenuation + Q(f) roll-off
 *  3. Sum segments at correct delays → dry thunder
 *  4. EnvironmentProcessor::apply() → ground reflection + terrain FDN + wind
 *  5. Normalise to target_rms
 *
 *  If crack_profile is set, it replaces the physics-derived spectral envelope for
 *  the closest segment (highest amplitude). Remaining segments (rumble) are always
 *  physics-generated.
 */
struct ThunderConfig {
  LightningConfig   lightning;
  PropagationConfig propagation;
  EnvironmentConfig environment;

  /** Analyzed crack profile from a real recording.
   *
   *  @details When set, this StochasticModel drives the crack component
   *  (closest, highest-amplitude segment) instead of the physics envelope.
   *  The rumble (remaining segments) is always physics-generated.
   *
   *  Obtain via:
   *  @code
   *    CrackAnalyzer analyzer;
   *    auto result = analyzer.analyze(audio);
   *    config.crack_profile = result.crack_model;
   *  @endcode
   *
   *  If empty(), ThunderSynth falls back to build_thunder_stochastic() using
   *  Few's formula for the source spectral peak (~65 Hz at 100 kA).
   */
  std::optional<analysis::StochasticModel> crack_profile;

  /** Target RMS of the output (linear, 0–1). Applied as a final gain stage. */
  float target_rms = 0.12f;

  /** Synthesis duration (s).
   *  If zero, auto-sized from bolt geometry:
   *  segment_spread + distance / v_air + tail decay. */
  double duration_s = 0.0;

  /** Binaural panning configuration used by synthesize_stereo().
   *
   *  @details Each lightning channel segment is rendered through
   *  spatial::StereoProcessor at its own 3-D position, so the listener
   *  hears independent left/right arrival times for every segment of
   *  the bolt. Set @c stereo.separation to 0 to render in mono within
   *  the stereo path (both ears identical). */
  spatial::StereoConfig stereo;

  /** @name Pre-crack stepped-leader precursor
   *
   *  @details When @p pre_crack_enabled is true, a faint high-frequency crackle
   *  (the stepped-leader acoustic emission) is injected @p pre_crack_offset_s
   *  seconds before the main crack.  The output buffer is extended accordingly.
   *  @{
   */
  bool   pre_crack_enabled  = false;
  double pre_crack_offset_s = PRE_CRACK_OFFSET_DEFAULT_S;  ///< Lead time before crack (s)
  double pre_crack_freq_hz  = PRE_CRACK_FREQ_DEFAULT_HZ;   ///< Spectral peak of precursor (Hz)
  float  pre_crack_level    = PRE_CRACK_AMPLITUDE_DEFAULT;  ///< Amplitude relative to crack
  /** @} */

  /** @name Stochastic tail crackles
   *
   *  @details After the main crack, discrete micro-crackles from kinked channel
   *  segments are added as a Poisson process whose rate decays with time.
   *  Based on Few (1969) "string of pearls" clap theory.
   *  @{
   */
  bool   tail_crackles_enabled = true;
  double crackle_rate_hz       = CRACKLE_RATE_DEFAULT_HZ;  ///< Initial events per second
  double crackle_decay_tau_s   = CRACKLE_DECAY_TAU_S;      ///< Rate decay time constant (s)
  /** @} */

  /** @name Low-frequency harmonic enhancement
   *
   *  @details Soft saturation applied to content below @p lf_saturation_hz
   *  to add sub-bass harmonics and perceived weight.  The saturated LF band
   *  is blended back at @p lf_saturation_mix.  Set @p lf_saturation_drive_db
   *  to 0 to disable.
   *  @{
   */
  double lf_saturation_hz       = LF_SAT_CORNER_HZ_DEFAULT;  ///< LPF corner (Hz)
  double lf_saturation_drive_db = LF_SAT_DRIVE_DB_DEFAULT;   ///< Soft-clip drive (dB)
  float  lf_saturation_mix      = LF_SAT_MIX_DEFAULT;        ///< Wet/dry blend
  /** @} */

  /** @name Independent stereo tail
   *
   *  @details synthesize_stereo() renders a secondary channel geometry with
   *  @p secondary_tail_seed for the rumble to produce genuinely independent
   *  left/right content.  When 0 (default), the seed is derived from
   *  @c lightning.rng_seed automatically.
   *  @{
   */
  uint32_t secondary_tail_seed = 0;
  /** @} */

  /** Lateral offset (m) of the entire bolt along the listener's @f$x@f$ axis.
   *  Positive values shift the strike to the listener's right, producing
   *  an overall right-leaning image. The randomised tortuous channel
   *  shape is preserved; only the centre of the bolt moves. */
  double strike_offset_x_m = 0.0;

  /** Forward offset (m) of the entire bolt along the listener's @f$y@f$ axis.
   *  Positive values place the strike in front of the listener, negative
   *  behind. Combined with @p strike_offset_x_m this places the bolt
   *  anywhere in the horizontal plane. */
  double strike_offset_y_m = 0.0;
};

/** @brief Top-level orchestrator for physics-based parametric thunder synthesis.
 *
 *  @details Produces a mono audio buffer from a ThunderConfig without audio
 *  analysis, oscillators, or samples. The deterministic component of the source
 *  pulse is represented entirely by a time-varying StochasticModel fed through
 *  NoiseGenerator; no sinusoidal partials are used.
 *
 *  The source spectral peak is derived from Few's (1969) formula:
 *  @f[ f_{\max} = 0.63 \cdot v_{\text{air}} \sqrt{\frac{P_0}{E_l}} \approx 65\,\text{Hz at}\;100\,\text{kA} @f]
 *  PropagationFilter subsequently shifts this downward with distance; at 5 km
 *  urban (low Q, high scattering) the received peak is ~35 Hz — do not use
 *  that value as a synthesis target.
 */
class ThunderSynth {
 public:
  ThunderSynth() = default;

  /** @brief Synthesize thunder from the given configuration.
   *  @param config  Full source, propagation, and environment parameters.
   *  @param sample_rate  Output sample rate (Hz).
   *  @return Mono audio buffer normalised to ThunderConfig::target_rms.
   */
  SampleBuffer synthesize(const ThunderConfig& config, double sample_rate) const;

  /** @brief Synthesize thunder as a 2-channel binaural buffer.
   *
   *  @details Same physical pipeline as synthesize(), but each channel
   *  segment is panned through spatial::StereoProcessor at its own
   *  3-D position. Adjacent segments arrive at slightly different times
   *  in each ear, producing a wide stereo image whose left/right balance
   *  evolves independently as the bolt rolls in.
   *
   *  Use ThunderConfig::stereo to control head width, separation factor,
   *  and head shadowing; use ThunderConfig::strike_offset_x_m and
   *  strike_offset_y_m to place the strike off-centre.
   *
   *  @param config  Full source, propagation, environment, and stereo parameters.
   *  @param sample_rate  Output sample rate (Hz).
   *  @return 2-channel AudioBuffer (channel 0 = left, channel 1 = right),
   *          peak-normalised to ThunderConfig::target_rms.
   */
  AudioBuffer synthesize_stereo(const ThunderConfig& config,
                                double sample_rate) const;

 private:
  // Build a StochasticModel for one segment using the thunder spectral profile.
  // peak_hz is from Few's formula for this segment; duration_s sets the decay envelope.
  analysis::StochasticModel build_thunder_stochastic(double peak_hz,
                                                     double duration_s,
                                                     double sample_rate) const;

  // Compute synthesis duration from bolt geometry when ThunderConfig::duration_s == 0.
  double auto_duration(const ThunderConfig& config) const;

  // Estimate positive-pulse duration t_p from discharge current.
  // Scales inversely with current_ka: higher current → sharper crack.
  double estimate_pulse_duration(double current_ka) const;
};

/** @brief Build a PropagationConfig for a specific channel segment.
 *
 *  Copies the base propagation config and overrides distance_m with
 *  LightningSegment::slant_distance_m.
 */
PropagationConfig propagation_for_segment(const PropagationConfig& base,
                                          const LightningSegment& segment);

}  // namespace thunder
}  // namespace sferic
