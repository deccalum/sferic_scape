#include "thunder/environment_model.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/constants.h"

namespace sferic {
namespace thunder {

// ── Surface properties — empirical, no citation ──────────────────────────────

namespace {

struct SurfaceProperties {
  double reflection_coeff;
  double lpf_cutoff_hz;
};

SurfaceProperties surface_props(SurfaceType s) {
  switch (s) {
    case SurfaceType::OpenWater:  return {0.95, 10000.0};
    case SurfaceType::Rock:       return {0.90,  8000.0};
    case SurfaceType::UrbanPaved: return {0.85,  6000.0};
    case SurfaceType::WetGrass:   return {0.60,  3000.0};
    case SurfaceType::DrySoil:    return {0.50,  2500.0};
    case SurfaceType::Forest:     return {0.30,  1500.0};
  }
  return {0.50, 2500.0};
}

// Simple one-pole LPF applied in-place.
void apply_one_pole_lpf(SampleBuffer& buf, double cutoff_hz, double sample_rate) {
  if (buf.empty() || cutoff_hz >= sample_rate * 0.5) return;
  double alpha = 1.0 - std::exp(-TWO_PI * cutoff_hz / sample_rate);
  double y = 0.0;
  for (size_t i = 0; i < buf.size(); ++i) {
    y += alpha * (static_cast<double>(buf[i]) - y);
    buf[i] = static_cast<Sample>(y);
  }
}

}  // namespace

// ── Ground reflection ────────────────────────────────────────────────────────

GroundReflection compute_ground_reflection(const EnvironmentConfig& env,
                                           double slant_distance_m) {
  auto props = surface_props(env.surface);

  // Path-length difference for ground-reflected ray.
  // Listener height ~1.7 m; elevation angle determines extra path.
  constexpr double kListenerHeight = 1.7;
  double theta_rad = env.source_elevation_deg * PI / 180.0;
  double sin_theta = std::max(std::sin(theta_rad), 0.01);

  // Extra path ≈ 2h sin(θ) for near-grazing, clamped to physical minimum
  double delay_s = 2.0 * kListenerHeight * sin_theta / SPEED_OF_SOUND;

  // Amplitude falls with distance (longer reflected path = weaker)
  double dist_atten = (slant_distance_m > 1.0)
                          ? 1.0 / std::sqrt(slant_distance_m / 100.0)
                          : 1.0;
  double amplitude = props.reflection_coeff * std::min(dist_atten, 1.0);

  return {delay_s, amplitude, props.lpf_cutoff_hz};
}

// ── EnvironmentProcessor ─────────────────────────────────────────────────────

EnvironmentProcessor::EnvironmentProcessor(const EnvironmentConfig& config,
                                           double sample_rate)
    : config_(config), sample_rate_(sample_rate) {
  // Scale FDN delays and gains by openness
  double delay_scale = 0.3 + 0.7 * static_cast<double>(config.openness);
  for (size_t i = 0; i < kFdnTaps; ++i) {
    fdn_delays_s_[i] *= delay_scale;
    fdn_gains_[i] *= static_cast<double>(config.openness);
  }
}

void EnvironmentProcessor::apply(SampleBuffer& audio) const {
  // Derive nominal slant distance from elevation for the generic apply().
  // Assumes ~2 km altitude source as default.
  constexpr double kDefaultAltitude = 2000.0;
  double theta_rad = config_.source_elevation_deg * PI / 180.0;
  double sin_theta = std::max(std::sin(theta_rad), 0.05);
  double nominal_slant = kDefaultAltitude / sin_theta;

  apply_ground_reflection(audio, nominal_slant);
  apply_terrain_scattering(audio);
  apply_wind(audio);
}

void EnvironmentProcessor::apply_ground_reflection(SampleBuffer& audio,
                                                    double slant_distance_m) const {
  if (audio.empty()) return;

  GroundReflection gr = compute_ground_reflection(config_, slant_distance_m);
  size_t delay_samples = static_cast<size_t>(gr.delay_s * sample_rate_);
  if (delay_samples == 0 || delay_samples >= audio.size()) return;

  // Create filtered copy of the reflected signal
  SampleBuffer reflected(audio);
  apply_one_pole_lpf(reflected, gr.lpf_cutoff_hz, sample_rate_);

  // Add delayed, scaled reflection
  for (size_t i = delay_samples; i < audio.size(); ++i) {
    audio[i] += static_cast<Sample>(gr.amplitude_scale) *
                reflected[i - delay_samples];
  }
}

void EnvironmentProcessor::apply_terrain_scattering(SampleBuffer& audio) const {
  if (config_.openness <= 0.001f || audio.empty()) return;

  // Snapshot the original audio to prevent feedback
  SampleBuffer snapshot(audio);

  for (size_t tap = 0; tap < kFdnTaps; ++tap) {
    size_t delay_samples = static_cast<size_t>(fdn_delays_s_[tap] * sample_rate_);
    if (delay_samples == 0 || delay_samples >= audio.size()) continue;

    auto gain = static_cast<Sample>(fdn_gains_[tap]);
    for (size_t i = delay_samples; i < audio.size(); ++i) {
      audio[i] += gain * snapshot[i - delay_samples];
    }
  }
}

void EnvironmentProcessor::apply_wind(SampleBuffer& audio) const {
  if (config_.wind_speed_ms < 0.01f || audio.empty()) return;

  // LFO rate: 0.5–2.0 Hz, scaled by wind speed (cap at 20 m/s)
  double speed_norm = std::min(static_cast<double>(config_.wind_speed_ms), 20.0) / 20.0;
  double lfo_rate = 0.5 + 1.5 * speed_norm;

  // Modulation depth: gentle, max ~15%
  double depth = 0.05 + 0.10 * speed_norm;

  // Amplitude modulation
  double dt = 1.0 / sample_rate_;
  for (size_t i = 0; i < audio.size(); ++i) {
    double t = static_cast<double>(i) * dt;
    double mod = 1.0 + depth * std::sin(TWO_PI * lfo_rate * t);
    audio[i] = static_cast<Sample>(static_cast<double>(audio[i]) * mod);
  }

  // Directional HF attenuation: crosswind (90°) has most effect
  double wind_rad = config_.wind_direction_deg * PI / 180.0;
  double crosswind_factor = std::abs(std::sin(wind_rad));
  // cutoff: 20 kHz at downwind, 8 kHz at full crosswind
  double hf_cutoff = 20000.0 - 12000.0 * crosswind_factor * speed_norm;
  if (hf_cutoff < sample_rate_ * 0.5) {
    apply_one_pole_lpf(audio, hf_cutoff, sample_rate_);
  }
}

double EnvironmentProcessor::q_scatter_multiplier() const {
  // openness 0 → 2.0 (minimal scattering, high Q)
  // openness 1 → 0.4 (heavy scattering, low Q)
  return 2.0 - 1.6 * static_cast<double>(config_.openness);
}

}  // namespace thunder
}  // namespace sferic
