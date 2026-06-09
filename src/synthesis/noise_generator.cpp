#include "synthesis/noise_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "core/constants.h"
#include "core/logger.h"

namespace sferic {
namespace synthesis {

NoiseGenerator::NoiseGenerator() : rng_(std::random_device{}()) {}

void NoiseGenerator::load_model(const analysis::SpectralEnvelope& model) {
  model_ = model;

  const size_t n_env = model_.num_bins();
  fft_size_ = 2;
  while (fft_size_ / 2 + 1 < n_env) fft_size_ *= 2;
  hop_size_ = fft_size_ / 4;  // 75% overlap — empirical

  window_.resize(fft_size_);
  for (size_t i = 0; i < fft_size_; ++i)
    window_[i] = 0.5 * (1.0 - std::cos(TWO_PI * static_cast<double>(i) / static_cast<double>(fft_size_ - 1)));
  double sum_w2 = 0.0;
  for (double w : window_) sum_w2 += w * w;
  // Power-normalised OLA gain — matches the analysis STFT normalisation so that
  // STFT(output) ≈ envelope[k] used during synthesis.
  ola_norm_ = static_cast<double>(hop_size_) / sum_w2 * static_cast<double>(fft_size_);

  synthesis_pos_ = 0;
  overlap_buffer_.assign(fft_size_, 0.0f);
}

void NoiseGenerator::clear() {
  model_.ms.clear();
  synthesis_pos_ = 0;
  std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
}

std::vector<double> NoiseGenerator::get_envelope_at_time(double t) const {
  if (t <= model_.ms.front().time_seconds) return model_.ms.front().envelope;
  if (t >= model_.ms.back().time_seconds)  return model_.ms.back().envelope;

  size_t idx = 0;
  for (size_t i = 1; i < model_.ms.size(); ++i) {
    if (model_.ms[i].time_seconds > t) { idx = i - 1; break; }
  }

  const auto& f0     = model_.ms[idx];
  const auto& f1     = model_.ms[idx + 1];
  const double alpha = (t - f0.time_seconds) / (f1.time_seconds - f0.time_seconds);
  const size_t n     = f0.envelope.size();

  std::vector<double> envelope(n);
  for (size_t i = 0; i < n; ++i)
    envelope[i] = f0.envelope[i] * (1.0 - alpha) + f1.envelope[i] * alpha;

  return envelope;
}

void NoiseGenerator::irfft(const std::vector<std::complex<double>>& input,
                           std::vector<float>& output) {
  const size_t N = output.size();
  const size_t num_bins = N / 2 + 1;

  for (size_t n = 0; n < N; ++n) {
    const double omega = TWO_PI * static_cast<double>(n) / static_cast<double>(N);
    double sum = input[0].real();

    for (size_t k = 1; k < num_bins - 1; ++k) {
      const double angle = omega * static_cast<double>(k);
      sum += 2.0 * (input[k] * std::complex<double>(std::cos(angle), std::sin(angle))).real();
    }

    if (N % 2 == 0 && num_bins > 1) {
      sum += input[num_bins - 1].real() * std::cos(omega * static_cast<double>(num_bins - 1));
    }

    output[n] = static_cast<float>(sum / static_cast<double>(N));
  }
}

void NoiseGenerator::synthesize_grain(double center_time, std::vector<float>& output) {
  output.resize(fft_size_);
  const auto envelope   = get_envelope_at_time(center_time);
  const size_t num_bins = fft_size_ / 2 + 1;

  std::uniform_real_distribution<double> phase_dist(0.0, TWO_PI);
  std::vector<std::complex<double>> spectrum(num_bins);

  spectrum[0] = {0.0, 0.0};  // zero DC — noise has no mean
  for (size_t k = 1; k < num_bins - 1; ++k) {
    const double env_idx = static_cast<double>(k) *
                           static_cast<double>(envelope.size()) /
                           static_cast<double>(num_bins);
    const size_t idx = std::min(static_cast<size_t>(env_idx), envelope.size() - 1);
    spectrum[k] = std::polar(envelope[idx], phase_dist(rng_));
  }
  // Nyquist bin must be real
  {
    const size_t k       = num_bins - 1;
    const double env_idx = static_cast<double>(k) *
                           static_cast<double>(envelope.size()) /
                           static_cast<double>(num_bins);
    const size_t idx = std::min(static_cast<size_t>(env_idx), envelope.size() - 1);
    spectrum[k]      = {envelope[idx], 0.0};
  }

  irfft(spectrum, output);
  for (size_t i = 0; i < fft_size_; ++i)
    output[i] *= static_cast<float>(window_[i]);
}

size_t NoiseGenerator::render(float* output, size_t num_samples, double start_time) {
  size_t samples_written = 0;
  const double dt = 1.0 / model_.sample_rate;
  const size_t total_grains = (num_samples + hop_size_ - 1) / hop_size_;
  size_t grain_idx = 0;

  while (samples_written < num_samples) {
    if (synthesis_pos_ == 0) {
      const double current_time = start_time + static_cast<double>(samples_written) * dt;
      std::vector<float> grain;
      synthesize_grain(current_time, grain);

      for (size_t i = 0; i < fft_size_; ++i)
        overlap_buffer_[i] += grain[i] * static_cast<float>(ola_norm_);

      SFERIC_PROGRESS(grain_idx, total_grains, current_time * 1000.0, 0);
      ++grain_idx;
    }

    const size_t samples_available = hop_size_ - synthesis_pos_;
    const size_t samples_to_copy = std::min(samples_available, num_samples - samples_written);

    for (size_t i = 0; i < samples_to_copy; ++i)
      output[samples_written + i] = overlap_buffer_[synthesis_pos_ + i];

    samples_written += samples_to_copy;
    synthesis_pos_ += samples_to_copy;

    if (synthesis_pos_ >= hop_size_) {
      for (size_t i = 0; i < fft_size_ - hop_size_; ++i)
        overlap_buffer_[i] = overlap_buffer_[i + hop_size_];
      for (size_t i = fft_size_ - hop_size_; i < fft_size_; ++i) overlap_buffer_[i] = 0.0f;
      synthesis_pos_ = 0;
    }
  }

  return num_samples;
}

}  // namespace synthesis
}  // namespace sferic
