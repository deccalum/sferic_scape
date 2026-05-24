#include "synthesis/noise_generator.h"

#include <algorithm>
#include <cmath>

#include "analysis/window.h"
#include "core/constants.h"

namespace sferic {
namespace synthesis {

NoiseGenerator::NoiseGenerator(double sample_rate)
    : sample_rate_(sample_rate), rng_(std::random_device{}()), dist_(0.0f, 1.0f) {
  overlap_buffer_.resize(fft_size_, 0.0f);
}

void NoiseGenerator::load_model(const analysis::StochasticModel& model) {
  model_ = model;
  synthesis_pos_ = 0;
  std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
}

void NoiseGenerator::clear() {
  model_.frames.clear();
  synthesis_pos_ = 0;
  std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
}

std::vector<double> NoiseGenerator::get_envelope_at_time(double t) const {
  if (model_.frames.empty()) {
    return std::vector<double>(model_.num_envelope_points, 0.0);
  }

  // Find surrounding frames
  if (t <= model_.frames.front().time_seconds) {
    return model_.frames.front().envelope;
  }
  if (t >= model_.frames.back().time_seconds) {
    return model_.frames.back().envelope;
  }

  // Binary search for the frame before t
  size_t idx = 0;
  for (size_t i = 1; i < model_.frames.size(); ++i) {
    if (model_.frames[i].time_seconds > t) {
      idx = i - 1;
      break;
    }
  }

  // Linear interpolation between frames
  const auto& f0 = model_.frames[idx];
  const auto& f1 = model_.frames[idx + 1];

  double t0 = f0.time_seconds;
  double t1 = f1.time_seconds;
  double alpha = (t - t0) / (t1 - t0);

  std::vector<double> envelope(model_.num_envelope_points);
  for (size_t i = 0; i < model_.num_envelope_points; ++i) {
    envelope[i] = f0.envelope[i] * (1.0 - alpha) + f1.envelope[i] * alpha;
  }

  return envelope;
}

void NoiseGenerator::generate_white_noise(float* buffer, size_t num_samples) {
  for (size_t i = 0; i < num_samples; ++i) {
    buffer[i] = dist_(rng_);
  }
}

void NoiseGenerator::rfft(const std::vector<float>& input,
                          std::vector<std::complex<double>>& output) {
  // Real-to-complex FFT (only compute positive frequencies)
  size_t N = input.size();
  size_t num_bins = N / 2 + 1;
  output.resize(num_bins);

  // Naive DFT implementation
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

void NoiseGenerator::irfft(const std::vector<std::complex<double>>& input,
                            std::vector<float>& output) {
  // Complex-to-real IFFT (reconstruct from positive frequencies only)
  size_t N = output.size();
  size_t num_bins = N / 2 + 1;

  // Naive IDFT implementation
  for (size_t n = 0; n < N; ++n) {
    double sum = 0.0;
    double omega = TWO_PI * static_cast<double>(n) / static_cast<double>(N);

    // DC component
    sum += input[0].real();

    // Positive frequencies (include conjugate symmetry)
    for (size_t k = 1; k < num_bins - 1; ++k) {
      double angle = omega * static_cast<double>(k);
      std::complex<double> twiddle(std::cos(angle), std::sin(angle));
      sum += 2.0 * (input[k] * twiddle).real();
    }

    // Nyquist component
    if (N % 2 == 0 && num_bins > 1) {
      sum += input[num_bins - 1].real() * std::cos(omega * static_cast<double>(num_bins - 1));
    }

    output[n] = static_cast<float>(sum / static_cast<double>(N));
  }
}

void NoiseGenerator::synthesize_grain(double center_time, std::vector<float>& output) {
  output.resize(fft_size_);

  // 1. Generate white noise
  generate_white_noise(output.data(), fft_size_);

  // 2. Apply window (Hann for smooth overlap-add)
  auto window = analysis::make_window(analysis::WindowType::HANN, fft_size_);
  for (size_t i = 0; i < fft_size_; ++i) {
    output[i] *= static_cast<float>(window[i]);
  }

  // 3. FFT to frequency domain
  std::vector<std::complex<double>> spectrum;
  rfft(output, spectrum);

  // 4. Get spectral envelope at this time
  auto envelope = get_envelope_at_time(center_time);

  // 5. Apply spectral shaping
  // Map envelope (128 points) to FFT bins (1025 points)
  size_t num_bins = spectrum.size();
  for (size_t k = 0; k < num_bins; ++k) {
    // Map FFT bin to envelope bin
    double env_idx = static_cast<double>(k) * static_cast<double>(envelope.size()) /
                     static_cast<double>(num_bins);
    size_t idx = std::min(static_cast<size_t>(env_idx), envelope.size() - 1);

    // Apply envelope magnitude (preserve random phase from noise)
    double magnitude = envelope[idx];
    spectrum[k] *= magnitude;
  }

  // 6. IFFT back to time domain
  irfft(spectrum, output);

  // 7. Apply window again for smooth overlap-add
  for (size_t i = 0; i < fft_size_; ++i) {
    output[i] *= static_cast<float>(window[i]);
  }
}

size_t NoiseGenerator::render(float* output, size_t num_samples, double start_time) {
  if (output == nullptr || num_samples == 0 || !has_model()) {
    // Zero output if no model
    if (output != nullptr) {
      for (size_t i = 0; i < num_samples; ++i) {
        output[i] = 0.0f;
      }
    }
    return num_samples;
  }

  // Overlap-add synthesis
  size_t samples_written = 0;
  double dt = 1.0 / sample_rate_;

  while (samples_written < num_samples) {
    // Check if we need a new grain
    if (synthesis_pos_ == 0) {
      // Synthesize new grain
      double current_time = start_time + static_cast<double>(samples_written) * dt;
      std::vector<float> grain;
      synthesize_grain(current_time, grain);

      // Overlap-add with buffer
      for (size_t i = 0; i < fft_size_; ++i) {
        overlap_buffer_[i] += grain[i];
      }
    }

    // Output samples from overlap buffer
    size_t samples_available = hop_size_ - synthesis_pos_;
    size_t samples_to_copy = std::min(samples_available, num_samples - samples_written);

    for (size_t i = 0; i < samples_to_copy; ++i) {
      output[samples_written + i] = overlap_buffer_[synthesis_pos_ + i];
    }

    samples_written += samples_to_copy;
    synthesis_pos_ += samples_to_copy;

    // Advance to next hop
    if (synthesis_pos_ >= hop_size_) {
      // Shift overlap buffer
      for (size_t i = 0; i < fft_size_ - hop_size_; ++i) {
        overlap_buffer_[i] = overlap_buffer_[i + hop_size_];
      }
      // Zero out the tail
      for (size_t i = fft_size_ - hop_size_; i < fft_size_; ++i) {
        overlap_buffer_[i] = 0.0f;
      }
      synthesis_pos_ = 0;
    }
  }

  return num_samples;
}

}  // namespace synthesis
}  // namespace sferic
