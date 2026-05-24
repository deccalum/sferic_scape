#include "thunder/lightning_model.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/constants.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

double peak_overpressure_pa(double current_ka) {
  double p = WANG_PRESSURE_SLOPE_PA_PER_KA * current_ka + WANG_PRESSURE_OFFSET_PA;
  return std::max(p, 0.0);
}

double peak_frequency_hz(double current_ka,
                         double ambient_pressure_pa,
                         double v_air_ms) {
  // Calibrate E_l so that Few's formula yields THUNDER_PEAK_FREQ_TYPICAL_HZ
  // (~65 Hz) at the reference current of 100 kA.
  //
  // Few (1969): f_max = FEW_K * v_air * sqrt(P_0 / E_l)
  // Solving for E_l at reference:
  //   E_l_ref = (FEW_K * v_air)^2 * P_0 / f_ref^2
  //
  // Energy scales as I^2: E_l = E_l_ref * (I / I_ref)^2
  //
  // Note: LIGHTNING_ENERGY_PER_LENGTH_J_M (4.0e6) is the total acoustic energy
  // value from Hong et al. (2023) that produces the propagated ~35 Hz peak.
  // We do NOT use it here — this function returns the SOURCE peak before
  // propagation, anchored to Abeygunawardana et al. near-field measurements.
  constexpr double kRefCurrentKa = 100.0;
  double fk_v = FEW_K * v_air_ms;
  double e_l_ref = (fk_v * fk_v) * ambient_pressure_pa /
                   (THUNDER_PEAK_FREQ_TYPICAL_HZ * THUNDER_PEAK_FREQ_TYPICAL_HZ);

  double current_ratio = current_ka / kRefCurrentKa;
  double e_l = e_l_ref * current_ratio * current_ratio;

  double f = fk_v * std::sqrt(ambient_pressure_pa / e_l);
  return std::clamp(f, THUNDER_PEAK_FREQ_MIN_HZ, THUNDER_FUNDAMENTAL_CEIL_HZ);
}

double source_pulse(double t, double t_p, double p_max) {
  if (t_p <= 0.0 || t < 0.0) return 0.0;

  double tau = t / t_p;
  double sqrt6 = std::sqrt(6.0);

  if (tau <= 1.0) {
    // Positive phase: linear decay from p_max to 0
    return p_max * (1.0 - tau);
  } else if (tau <= 1.0 + sqrt6) {
    // Negative phase: undershoot and recovery
    return (p_max / 6.0) * (1.0 - tau) *
           (1.0 + sqrt6 - tau) * (1.0 + sqrt6 - tau);
  }
  return 0.0;
}

SampleBuffer generate_segment_burst(double t_p_s,
                                    double duration_s,
                                    double sample_rate,
                                    uint32_t rng_seed) {
  size_t num_samples = static_cast<size_t>(duration_s * sample_rate);
  if (num_samples == 0) return {};

  std::mt19937 rng(rng_seed == 0 ? std::random_device{}() : rng_seed);
  std::normal_distribution<float> noise(0.0f, 1.0f);

  SampleBuffer buffer(num_samples);
  double dt = 1.0 / sample_rate;

  for (size_t i = 0; i < num_samples; ++i) {
    double t = static_cast<double>(i) * dt;
    double envelope = std::abs(source_pulse(t, t_p_s, 1.0));
    buffer[i] = static_cast<Sample>(envelope) * noise(rng);
  }

  // Normalise to [-1, 1]
  float peak = 0.0f;
  for (size_t i = 0; i < num_samples; ++i) {
    peak = std::max(peak, std::abs(buffer[i]));
  }
  if (peak > 0.0f) {
    float inv_peak = 1.0f / peak;
    for (size_t i = 0; i < num_samples; ++i) {
      buffer[i] *= inv_peak;
    }
  }

  return buffer;
}

std::vector<LightningSegment> make_channel(const LightningConfig& config) {
  std::mt19937 rng(config.rng_seed);
  std::normal_distribution<double> lateral_dist(0.0, config.bolt_length_m / 20.0);

  size_t n = config.num_segments;
  if (n == 0) return {};

  double segment_spread = config.segment_spread_s;
  if (segment_spread <= 0.0) {
    segment_spread = config.bolt_length_m / SPEED_OF_SOUND;
  }

  std::vector<LightningSegment> segments(n);

  // Place segments along a vertical bolt with random lateral offsets (tortuosity)
  double min_slant = std::numeric_limits<double>::max();

  for (size_t i = 0; i < n; ++i) {
    // Vertical position: nearest bolt point at altitude_m, extending upward.
    // altitude_m is the height of the lowest segment directly above the listener;
    // the bolt channel extends to altitude_m + bolt_length_m at the far end.
    double frac = (n == 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(n - 1);
    double z = config.altitude_m + frac * config.bolt_length_m;

    double dx = lateral_dist(rng);
    double dy = lateral_dist(rng);

    double ground_dist = std::sqrt(dx * dx + dy * dy);
    double slant_dist = std::sqrt(dx * dx + dy * dy + z * z);
    if (slant_dist < 1.0) slant_dist = 1.0;  // avoid division by zero

    segments[i].ground_distance_m = ground_dist;
    segments[i].slant_distance_m = slant_dist;
    segments[i].position_x_m = dx;
    segments[i].position_y_m = dy;
    segments[i].position_z_m = z;
    segments[i].peak_frequency_hz = (config.peak_freq_override_hz > 0.0)
                                        ? config.peak_freq_override_hz
                                        : peak_frequency_hz(config.current_ka);

    if (slant_dist < min_slant) {
      min_slant = slant_dist;
    }
  }

  // Compute delays and amplitude scales relative to closest segment
  double min_arrival = min_slant / SPEED_OF_SOUND;

  for (size_t i = 0; i < n; ++i) {
    double arrival = segments[i].slant_distance_m / SPEED_OF_SOUND;
    segments[i].delay_s = arrival - min_arrival;
    segments[i].amplitude_scale = min_slant / segments[i].slant_distance_m;
  }

  return segments;
}

}  // namespace thunder
}  // namespace sferic
