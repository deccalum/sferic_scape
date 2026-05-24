#pragma once

#include <cstddef>
#include <vector>

#include "core/types.h"

namespace sferic {
namespace thunder {

/** @brief Ground surface material affecting reflection amplitude and HF absorption. */
enum class SurfaceType {
  OpenWater,   ///< High reflection coefficient, specular, nearly lossless
  WetGrass,    ///< Moderate absorption, soft reflection
  DrySoil,     ///< Moderate-high absorption
  Forest,      ///< High diffuse scattering, absorptive
  Rock,        ///< High reflection, specular
  UrbanPaved,  ///< High reflection with additional structural scattering
};

/** @brief Environmental parameters for spatial post-processing of propagated thunder.
 *
 *  @p openness is the primary control. It governs the density of sparse terrain
 *  reflections (4-tap FDN) and the effective Q scatter multiplier applied during
 *  propagation. At openness = 0 only a single ground reflection is added;
 *  at openness = 1 long-delay echoes model canyon or urban enclosure.
 *
 *  @note This is not a room reverb. Outdoor thunder has no dense reverberant
 *        field. The FDN taps represent sparse specular returns from distant
 *        terrain features, not a diffuse tail.
 */
struct EnvironmentConfig {
  /** Spatial openness (0 = open sea, 1 = enclosed canyon / dense urban).
   *  Controls FDN feedback level and terrain echo density. */
  float openness = 0.5f;

  SurfaceType surface = SurfaceType::DrySoil;

  /** Wind speed (m s⁻¹) and direction relative to the source→listener axis.
   *  0° = headwind, 90° = crosswind, 180° = tailwind. */
  float wind_speed_ms      = 0.0f;
  float wind_direction_deg = 90.0f;

  float source_elevation_deg = 20.0f; ///< Elevation angle to nearest segment (°). Affects ground reflection delay.
  float source_azimuth_deg   = 0.0f;  ///< Horizontal azimuth to storm (°, 0 = front). Affects directional HF roll-off.
};

/** @brief Derived parameters for the ground-plane reflection.
 *
 *  Not set directly by the user; computed from EnvironmentConfig and
 *  the hypocentral distance of the closest lightning segment.
 */
struct GroundReflection {
  double delay_s;         ///< Path-length difference (s)
  double amplitude_scale; ///< Reflection coefficient, surface-dependent
  double lpf_cutoff_hz;   ///< LPF cut-off for absorptive surface HF loss (Hz)
};

/** @brief Compute ground reflection parameters for a given environment and distance. */
GroundReflection compute_ground_reflection(const EnvironmentConfig& env,
                                           double slant_distance_m);

/** @brief Applies post-propagation spatial and environmental shaping to thunder audio.
 *
 *  @details Processing order: ground reflection → sparse FDN returns → wind modulation.
 *
 *  The 4-tap FDN uses long, uncoupled delay lines (no inter-tap matrix) to produce
 *  sparse echoes rather than a dense reverberant tail:
 *  - Tap delays: 0.31, 0.57, 0.89, 1.43 s (scaled by openness)
 *  - Initial tap gains: 0.45, 0.30, 0.18, 0.10
 *
 *  Wind modulation applies a slow amplitude LFO (0.5–2 Hz) and frequency-dependent
 *  attenuation for crosswind paths.
 *
 *  The q_scatter_multiplier() value is used by PropagationFilter to adjust Q₀
 *  before distance attenuation is computed; call it before constructing the filter.
 */
class EnvironmentProcessor {
 public:
  EnvironmentProcessor(const EnvironmentConfig& config, double sample_rate);

  /** @brief Apply all environment effects to @p audio in-place. */
  void apply(SampleBuffer& audio) const;

  /** @brief Add a single ground-plane reflection to @p audio. */
  void apply_ground_reflection(SampleBuffer& audio, double slant_distance_m) const;

  /** @brief Add sparse terrain reflections via 4-tap FDN.
   *  No-op when EnvironmentConfig::openness is zero.
   */
  void apply_terrain_scattering(SampleBuffer& audio) const;

  /** @brief Apply wind amplitude modulation and directional HF attenuation. */
  void apply_wind(SampleBuffer& audio) const;

  /** @brief Q scatter multiplier for use by PropagationFilter.
   *
   *  @details Maps openness to an effective scattering adjustment:
   *  - openness = 0 → ~2.0 (minimal scattering, high Q)
   *  - openness = 1 → ~0.4 (heavy scattering, low Q)
   */
  double q_scatter_multiplier() const;

 private:
  EnvironmentConfig config_;
  double sample_rate_;

  static constexpr size_t kFdnTaps = 4;
  double fdn_delays_s_[kFdnTaps] = {0.31, 0.57, 0.89, 1.43};  // empirical — no citation
  double fdn_gains_[kFdnTaps]    = {0.45, 0.30, 0.18, 0.10};
};

}  // namespace thunder
}  // namespace sferic
