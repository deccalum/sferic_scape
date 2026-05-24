#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/types.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

/** @brief Parameters describing a single lightning discharge event.
 *
 *  Controls both the source amplitude (via @p current_ka) and the temporal
 *  structure of the resulting thunder (via @p bolt_length_m and
 *  @p num_segments). All segments are synthesised from the same discharge
 *  but arrive at the listener at different times.
 */
struct LightningConfig {
  double current_ka    = 100.0;   ///< Discharge current (kA). Natural range: 10–200 kA.
  double bolt_length_m = 3000.0;  ///< Total channel length (m). Sets rumble duration.
  double altitude_m    = 2000.0;  ///< Altitude of nearest bolt point above listener (m).
  size_t num_segments  = 16;      ///< Tortuous channel segments. Practical range: 8–32.

  /** Maximum additional arrival delay across all segments (s).
   *  If zero, auto-derived from @p bolt_length_m / SPEED_OF_SOUND. */
  double segment_spread_s = 0.0;

  /** Override for the source spectral peak (Hz).
   *  If > 0, replaces Few's formula (peak_frequency_hz). Used to match a
   *  reference recording whose dominant energy sits well above the
   *  physics-derived ~65 Hz at 100 kA — for example, very close strikes
   *  whose unattenuated source content extends into the 200–700 Hz band. */
  double peak_freq_override_hz = 0.0;

  /** Lowpass cutoff factor used by the per-segment spectral shaper.
   *  The 2nd-order Butterworth LP cutoff is set to
   *  @f$ \mathrm{lp\_cutoff\_factor} \times \mathrm{peak\_frequency\_hz} @f$.
   *  Larger values → wider bandwidth above the peak. Default 1.5 gives
   *  the f^{-2} rolloff predicted by Few (1969); raise it for close
   *  strikes where unattenuated HF content survives. */
  double lp_cutoff_factor = 1.5;

  /** Highpass cutoff factor used by the per-segment spectral shaper.
   *  The 2nd-order Butterworth HP cutoff is set to
   *  @f$ \mathrm{hp\_cutoff\_factor} \times \mathrm{peak\_frequency\_hz} @f$.
   *  Smaller values → broader bandwidth below the peak. Default 0.5
   *  centres the bandpass on @c peak_frequency_hz. */
  double hp_cutoff_factor = 0.5;

  /** Q of the resonant peaking EQ applied at @c peak_frequency_hz.
   *  Cookbook biquad peaking-EQ (Q ≈ centre / bandwidth_3dB).
   *  Set 0 to disable. Higher values → narrower, more concentrated peak.
   *  Real recordings show 6–14 dB of concentrated energy at the dominant
   *  resonance that broadband bandpass-filtered noise cannot reproduce;
   *  this lets the synth match those measured peaks. */
  double resonance_q = 0.0;

  /** Gain (dB) of the resonant peaking EQ at @c peak_frequency_hz.
   *  Only applied when @c resonance_q > 0. Empirical — no citation. */
  double resonance_gain_db = 0.0;

  /** Low-shelf cut shelf frequency (Hz).
   *  All energy below this frequency is attenuated by @c lf_shelf_gain_db.
   *  Set 0 to disable. Useful for removing infrasound and deep rumble that
   *  the HP filter cannot fully suppress. Empirical — no citation. */
  double lf_shelf_hz = 0.0;

  /** Low-shelf cut gain (dB). Should be negative (e.g. −15) to reduce
   *  infrasound/rumble content. Only applied when @c lf_shelf_hz > 0. */
  double lf_shelf_gain_db = 0.0;

  uint32_t rng_seed = 42;  ///< Fixed seed for reproducible channel geometry.
};

/** @brief One discrete segment of the tortuous lightning channel.
 *
 *  Few (1969) "string of pearls" model: the bolt is represented as a
 *  sequence of independent acoustic sources, each emitting a pressure
 *  pulse that arrives at the listener with a different delay.
 */
struct LightningSegment {
  double delay_s;           ///< Arrival delay relative to the closest segment (s).
  double ground_distance_m; ///< Horizontal distance to segment ground projection (m).
  double slant_distance_m;  ///< 3-D hypocentral distance from listener to segment (m).

  /** Cartesian position of the segment in the listener frame (m).
   *  Right-handed: +x = right, +y = forward, +z = up. The listener stands
   *  at the origin. Used by spatial::StereoProcessor for binaural panning. */
  double position_x_m;
  double position_y_m;
  double position_z_m;

  /** Amplitude scale relative to the closest segment.
   *  Accounts for distance fall-off and channel orientation: segments
   *  perpendicular to the observer produce a clap; parallel segments produce
   *  weaker rumble. */
  double amplitude_scale;

  double peak_frequency_hz; ///< Source spectral peak for this segment (Hz), from Few's formula.
};

/** @brief Peak overpressure at source from discharge current.
 *
 *  @details Wang et al. (2019):
 *  @f[ P_{\max} = 0.432 \cdot C_{\max} - 0.19 @f]
 *
 *  @param current_ka Discharge current (kA).
 *  @return Peak overpressure (Pa).
 */
double peak_overpressure_pa(double current_ka);

/** @brief Source spectral peak frequency from Few's cylindrical blast formula.
 *
 *  @details Few (1969):
 *  @f[ f_{\max} = 0.63 \cdot v_{\text{air}} \sqrt{\frac{P_0}{E_l}} @f]
 *  where @f$ E_l @f$ is estimated from @p current_ka via Wang et al. (2019).
 *
 *  Returns the **source** (near-field) peak before propagation. Calibrated to
 *  ~65 Hz at 100 kA (Abeygunawardana et al.). PropagationFilter shifts this
 *  to ~35 Hz at 5 km urban — do not use that value as a synthesis target.
 *
 *  @param current_ka Discharge current (kA).
 *  @return Source spectral peak (Hz).
 */
double peak_frequency_hz(double current_ka,
                         double ambient_pressure_pa = 101325.0,
                         double v_air_ms = 343.0);

/** @brief Evaluate the explosion source pressure pulse @f$ P_0(t) @f$.
 *
 *  @details Hong et al. (2023), eq. 2 / Kim et al. (2021):
 *  @f[
 *    P_0(t) = \begin{cases}
 *      P_{\max}\!\left(1 - \tfrac{t}{t_p}\right) & 0 \le \tfrac{t}{t_p} \le 1 \\
 *      \tfrac{P_{\max}}{6}\!\left(1-\tfrac{t}{t_p}\right)
 *        \!\left(1+\sqrt{6}-\tfrac{t}{t_p}\right)^2 & 1 < \tfrac{t}{t_p} \le 1+\sqrt{6}
 *    \end{cases}
 *  @f]
 *
 *  @param t     Time since segment discharge (s).
 *  @param t_p   Positive-pulse duration (s). Shorter = sharper crack.
 *  @param p_max Peak overpressure (Pa). Use peak_overpressure_pa().
 *  @return Instantaneous pressure (Pa).
 */
double source_pulse(double t, double t_p, double p_max);

/** @brief Generate a single-segment dry waveform shaped to the source pulse envelope.
 *
 *  Produces white noise modulated by the amplitude envelope of @p source_pulse.
 *  Spectral shaping is applied separately in ThunderSynth via StochasticModel.
 *  Output is normalised to ±1; ThunderSynth rescales with peak_overpressure_pa().
 */
SampleBuffer generate_segment_burst(double t_p_s,
                                    double duration_s,
                                    double sample_rate,
                                    uint32_t rng_seed = 0);

/** @brief Build a tortuous channel as a list of discrete LightningSegments.
 *
 *  Segment positions are randomised within the bolt geometry using
 *  LightningConfig::rng_seed. Delays, distances, and amplitude scales are
 *  all derived from geometry; no per-segment tuning is required.
 */
std::vector<LightningSegment> make_channel(const LightningConfig& config);

}  // namespace thunder
}  // namespace sferic
