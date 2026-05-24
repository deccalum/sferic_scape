#include "thunder/propagation_model.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "analysis/window.h"
#include "core/constants.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

// ── PropagationFilter ────────────────────────────────────────────────────────

PropagationFilter::PropagationFilter(const PropagationConfig& config,
                                     double sample_rate,
                                     size_t fft_size)
    : config_(config), sample_rate_(sample_rate), fft_size_(fft_size) {}

double PropagationFilter::q0() const {
  switch (config_.spreading) {
    case SpreadingMode::Cylindrical: return THUNDER_Q0_CYLINDRICAL;
    case SpreadingMode::WeakShock:   return THUNDER_Q0_WEAK_SHOCK;
    case SpreadingMode::Spherical:   return THUNDER_Q0_SPHERICAL;
  }
  return THUNDER_Q0_CYLINDRICAL;
}

double PropagationFilter::qexp() const {
  switch (config_.spreading) {
    case SpreadingMode::Cylindrical: return THUNDER_QEXP_CYLINDRICAL;
    case SpreadingMode::WeakShock:   return THUNDER_QEXP_WEAK_SHOCK;
    case SpreadingMode::Spherical:   return THUNDER_QEXP_SPHERICAL;
  }
  return THUNDER_QEXP_CYLINDRICAL;
}

double PropagationFilter::spreading_gain() const {
  if (config_.distance_m <= ref_distance_m_) return 1.0;

  double n = SPREADING_N_CYLINDRICAL;
  switch (config_.spreading) {
    case SpreadingMode::Cylindrical: n = SPREADING_N_CYLINDRICAL; break;
    case SpreadingMode::WeakShock:   n = SPREADING_N_WEAK_SHOCK;  break;
    case SpreadingMode::Spherical:   n = SPREADING_N_SPHERICAL;   break;
  }
  return std::pow(ref_distance_m_ / config_.distance_m, n);
}

double PropagationFilter::q_factor(double f_hz) const {
  if (f_hz <= 0.0) return 1.0e12;  // no attenuation at DC
  return q0() * std::pow(f_hz, qexp());
}

double PropagationFilter::atmospheric_absorption_db_per_km(double f_hz) const {
  if (f_hz <= 0.0) return 0.0;

  // Linear interpolation over ABSORPTION_TABLE from core/constants.h
  if (f_hz <= ABSORPTION_TABLE[0].frequency_hz) {
    return ABSORPTION_TABLE[0].db_per_km;
  }
  if (f_hz >= ABSORPTION_TABLE[ABSORPTION_TABLE_SIZE - 1].frequency_hz) {
    return ABSORPTION_TABLE[ABSORPTION_TABLE_SIZE - 1].db_per_km;
  }

  for (size_t i = 1; i < ABSORPTION_TABLE_SIZE; ++i) {
    if (f_hz <= ABSORPTION_TABLE[i].frequency_hz) {
      double f0 = ABSORPTION_TABLE[i - 1].frequency_hz;
      double f1 = ABSORPTION_TABLE[i].frequency_hz;
      double d0 = ABSORPTION_TABLE[i - 1].db_per_km;
      double d1 = ABSORPTION_TABLE[i].db_per_km;
      double frac = (f_hz - f0) / (f1 - f0);
      return d0 + frac * (d1 - d0);
    }
  }
  return ABSORPTION_TABLE[ABSORPTION_TABLE_SIZE - 1].db_per_km;
}

double PropagationFilter::total_gain(double f_hz) const {
  double sg = spreading_gain();

  // Q attenuation: exp(-π f r / (v_air Q(f)))
  double Q = q_factor(f_hz);
  double q_atten = std::exp(-PI * f_hz * config_.distance_m /
                            (SPEED_OF_SOUND * Q));

  // Atmospheric absorption
  double abs_db = atmospheric_absorption_db_per_km(f_hz) *
                  config_.distance_m / 1000.0;
  double abs_lin = std::pow(10.0, -abs_db / 20.0);

  return sg * q_atten * abs_lin;
}

// ── Naive FFT helpers ────────────────────────────────────────────────────────

namespace {

void rfft(const std::vector<float>& input,
          std::vector<std::complex<double>>& output) {
  size_t N = input.size();
  size_t num_bins = N / 2 + 1;
  output.resize(num_bins);

  for (size_t k = 0; k < num_bins; ++k) {
    std::complex<double> sum(0.0, 0.0);
    double omega = -TWO_PI * static_cast<double>(k) / static_cast<double>(N);
    for (size_t n = 0; n < N; ++n) {
      double angle = omega * static_cast<double>(n);
      sum += static_cast<double>(input[n]) *
             std::complex<double>(std::cos(angle), std::sin(angle));
    }
    output[k] = sum;
  }
}

void irfft(const std::vector<std::complex<double>>& input,
           std::vector<float>& output) {
  size_t N = output.size();
  size_t num_bins = N / 2 + 1;

  for (size_t n = 0; n < N; ++n) {
    double sum = 0.0;
    double omega = TWO_PI * static_cast<double>(n) / static_cast<double>(N);

    sum += input[0].real();

    for (size_t k = 1; k < num_bins - 1; ++k) {
      double angle = omega * static_cast<double>(k);
      std::complex<double> twiddle(std::cos(angle), std::sin(angle));
      sum += 2.0 * (input[k] * twiddle).real();
    }

    if (N % 2 == 0 && num_bins > 1) {
      size_t last = num_bins - 1;
      sum += input[last].real() *
             std::cos(omega * static_cast<double>(last));
    }

    output[n] = static_cast<float>(sum / static_cast<double>(N));
  }
}

}  // namespace

void PropagationFilter::apply(SampleBuffer& audio) const {
  if (audio.empty()) return;

  size_t N = fft_size_;
  size_t hop = N / 2;
  size_t num_bins = N / 2 + 1;
  size_t audio_len = audio.size();

  // Precompute per-bin gain
  std::vector<double> bin_gains(num_bins);
  for (size_t k = 0; k < num_bins; ++k) {
    double f_hz = static_cast<double>(k) * sample_rate_ /
                  static_cast<double>(N);
    bin_gains[k] = total_gain(f_hz);
  }

  auto window = analysis::make_window(analysis::WindowType::HANN, N);

  // Output accumulator
  std::vector<double> output(audio_len + N, 0.0);

  // Overlap-add processing
  for (size_t pos = 0; pos + N <= audio_len + hop; pos += hop) {
    // Extract and window input block
    std::vector<float> block(N, 0.0f);
    for (size_t i = 0; i < N; ++i) {
      size_t idx = pos + i;
      if (idx < audio_len) {
        block[i] = audio[idx] * static_cast<float>(window[i]);
      }
    }

    // Forward FFT
    std::vector<std::complex<double>> spectrum;
    rfft(block, spectrum);

    // Apply frequency-domain gain
    for (size_t k = 0; k < num_bins; ++k) {
      spectrum[k] *= bin_gains[k];
    }

    // Inverse FFT
    std::vector<float> time_block(N);
    irfft(spectrum, time_block);

    // Window again and overlap-add
    for (size_t i = 0; i < N; ++i) {
      size_t idx = pos + i;
      if (idx < output.size()) {
        output[idx] += static_cast<double>(time_block[i]) * window[i];
      }
    }
  }

  // Normalise for Hann window overlap-add (hop = N/2 with double windowing)
  // Hann^2 with 50% overlap sums to 0.375*N per sample; normalise accordingly
  double window_sq_sum = 0.0;
  for (size_t i = 0; i < N; ++i) {
    window_sq_sum += window[i] * window[i];
  }
  double norm = window_sq_sum / static_cast<double>(hop);
  if (norm > 0.0) {
    double inv_norm = 1.0 / norm;
    for (size_t i = 0; i < audio_len; ++i) {
      audio[i] = static_cast<Sample>(output[i] * inv_norm);
    }
  }
}

}  // namespace thunder
}  // namespace sferic
