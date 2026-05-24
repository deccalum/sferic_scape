#pragma once

#include <cstddef>

#include "core/buffer.h"
#include "core/types.h"

namespace sferic {
namespace spatial {

/** @brief 3-D position of a sound source relative to the listener.
 *
 *  Right-handed coordinate system. The listener stands at the origin
 *  facing along @f$+y@f$, ears separated along the @f$x@f$ axis:
 *
 *    +x = to the listener's right
 *    +y = in front of the listener
 *    +z = above the listener
 *
 *  All units are metres. A source directly overhead is `(0, 0, h)`,
 *  one to the side is `(d, 0, 0)`, one straight ahead is `(0, d, 0)`.
 */
struct Position {
  double x_m = 0.0;  ///< Right (+) / left (-) of the listener.
  double y_m = 0.0;  ///< Front (+) / behind (-) the listener.
  double z_m = 0.0;  ///< Above (+) / below (-) the listener.
};

/** @brief Configuration for the binaural stereo processor.
 *
 *  @details Models the listener as two ears separated horizontally by
 *  @p head_width_m. Each ear receives an independently delayed and
 *  attenuated copy of the source signal:
 *
 *  @f[
 *    d_{L} = \sqrt{(x + w/2)^2 + y^2 + z^2}, \qquad
 *    d_{R} = \sqrt{(x - w/2)^2 + y^2 + z^2}
 *  @f]
 *
 *  giving an interaural time difference (ITD)
 *  @f$ \mathrm{ITD} = (d_{R} - d_{L}) / v_{\text{air}} @f$
 *  and a per-ear amplitude proportional to @f$ 1/d @f$ (inverse-distance law).
 *  An optional 1st-order lowpass on the far ear approximates the
 *  high-frequency rolloff caused by the head shadowing acoustic
 *  diffraction.
 *
 *  @note The model is purely geometric. It captures ITD and ILD but not
 *  pinna cues or precise HRTF colouration. It is intentionally cheap so
 *  hundreds of distributed sources (lightning channel segments, for
 *  example) can be panned independently in real time.
 */
struct StereoConfig {
  /** Inter-ear distance (m). Adult human average ≈ 0.18 m.
   *  Larger values exaggerate ITD; smaller values shrink the stereo image. */
  double head_width_m = 0.18;

  /** Speed of sound (m/s). Defaults to dry air at 20 °C. */
  double speed_of_sound_ms = 343.0;

  /** Stereo separation factor in [0, 1].
   *
   *  - `0.0` — mono: both ears receive the centred response (no ITD, no ILD).
   *  - `1.0` — full physical model.
   *
   *  Intermediate values linearly interpolate the per-ear delay and gain
   *  against the centred response, allowing the effect to be dialled in
   *  without changing source positions. */
  double separation = 1.0;

  /** When true, the closer ear's gain is normalised to 1.0 and the far
   *  ear is scaled accordingly. Useful when summing many distant sources
   *  whose absolute @f$1/d@f$ gains would otherwise be vanishingly small.
   *  When false, the raw inverse-distance gain is used (correct for
   *  near-field sources within a few metres). */
  bool normalise_gain = true;

  /** When true, apply a 1st-order lowpass to the far ear to simulate
   *  head-shadow high-frequency rolloff. */
  bool head_shadow = true;

  /** Cutoff (Hz) of the head-shadow lowpass at 90° azimuth (fully lateral
   *  source). The shadow weakens linearly toward the frontal/rear plane
   *  where the cutoff approaches Nyquist (no filtering).
   *  Empirical default ≈ 2 kHz; below this the head doesn't shadow much. */
  double head_shadow_cutoff_hz = 2000.0;
};

/** @brief Per-ear geometry for a single source position.
 *
 *  Computed by StereoProcessor::compute_frame(). The two ears are
 *  evaluated independently and share no state, so two sources at
 *  different positions can be summed without cross-talk.
 */
struct StereoFrame {
  double left_distance_m;   ///< Source-to-left-ear straight-line distance.
  double right_distance_m;  ///< Source-to-right-ear straight-line distance.
  double left_delay_s;      ///< Travel time from source to left ear.
  double right_delay_s;     ///< Travel time from source to right ear.
  double left_gain;         ///< Linear amplitude scale for the left ear.
  double right_gain;        ///< Linear amplitude scale for the right ear.

  /** Interaural time difference @f$ d_R/v - d_L/v @f$ (s).
   *  Positive when the source is on the listener's left (right ear lags). */
  double itd_s;

  /** Source azimuth in the horizontal plane (rad).
   *  @f$ \mathrm{atan2}(x, y) @f$ — 0 directly ahead, @f$+\pi/2@f$ to the right. */
  double azimuth_rad;
};

/** @brief Stateless binaural panner.
 *
 *  @details Renders mono sound sources at fixed 3-D positions into a
 *  stereo AudioBuffer. Each ear is processed independently:
 *  -# the source is fractionally delayed by its per-ear travel time
 *     (linear interpolation between adjacent input samples),
 *  -# scaled by the inverse-distance gain (optionally normalised),
 *  -# optionally lowpass-filtered to simulate head shadowing.
 *
 *  Multiple sources can be summed by calling render_into() once per
 *  source on the same output buffer. Because each call computes its own
 *  per-ear delays from the source position, distributed sources (such as
 *  the segments of a lightning bolt) naturally produce *independent*
 *  left/right arrival times — exactly the behaviour the user perceives
 *  as a wide, moving thunder image.
 *
 *  The processor is stateless: there is no internal delay line, no
 *  smoothing of moving sources, and the same StereoProcessor instance
 *  may be reused across unrelated render calls.
 */
class StereoProcessor {
 public:
  explicit StereoProcessor(const StereoConfig& config = {});

  const StereoConfig& config() const {
    return config_;
  }

  /** @brief Compute per-ear delay and gain for a source position. */
  StereoFrame compute_frame(const Position& source) const;

  /** @brief Render a mono source into a fresh 2-channel AudioBuffer.
   *
   *  @param mono         Mono input samples.
   *  @param source       3-D source position.
   *  @param sample_rate  Output sample rate (Hz).
   *  @param output_frames  Length of the output buffer. If 0, sized to
   *                        @p mono.size() plus the maximum per-ear delay.
   *  @return 2-channel AudioBuffer (channel 0 = left, channel 1 = right).
   */
  AudioBuffer render(const SampleBuffer& mono,
                     const Position& source,
                     double sample_rate,
                     size_t output_frames = 0) const;

  /** @brief Render a mono source and add it into an existing stereo buffer.
   *
   *  Used for multi-source rendering: call once per source, summing into
   *  the same buffer. The output buffer must already be 2-channel and
   *  sized to hold the result; this function does not resize it.
   *
   *  @param mono           Mono input samples.
   *  @param source         3-D source position.
   *  @param stereo_out     Existing 2-channel output buffer (added into).
   *  @param frame_offset   Output frame at which to start writing.
   */
  void render_into(const SampleBuffer& mono,
                   const Position& source,
                   AudioBuffer& stereo_out,
                   size_t frame_offset = 0) const;

  /** @brief Constant-power equal-energy panner — no per-ear delay.
   *
   *  Used as a fallback when full physical modelling is unnecessary
   *  (e.g. summing pre-mixed stems where ITD has already been baked in).
   *
   *  @param mono         Mono input samples.
   *  @param pan          Pan position in [-1, 1] (-1 = full left, +1 = full right).
   *  @param sample_rate  Sample rate of the resulting AudioBuffer.
   */
  static AudioBuffer constant_power_pan(const SampleBuffer& mono,
                                        double pan,
                                        double sample_rate);

 private:
  StereoConfig config_;
};

}  // namespace spatial
}  // namespace sferic
