#include "analysis/stft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/constants.h"

#ifdef SFERIC_HAS_FFTW3
#include <fftw3.h>
#endif

namespace sferic {
namespace analysis {

struct STFT::Impl {
  size_t fft_size = 0;
  size_t num_bins = 0;

#ifdef SFERIC_HAS_FFTW3
  fftw_plan plan = nullptr;
  double* fft_in = nullptr;
  fftw_complex* fft_out = nullptr;

  ~Impl() {
    if (plan) fftw_destroy_plan(plan);
    if (fft_in) fftw_free(fft_in);
    if (fft_out) fftw_free(fft_out);
  }
#else
  // Fallback: store temp buffers for naive DFT
  std::vector<double> fft_in;
  std::vector<double> fft_out_real;
  std::vector<double> fft_out_imag;
#endif

  void execute(const double* windowed_input, std::vector<double>& magnitudes,
               std::vector<double>& phases);
};

void STFT::Impl::execute(const double* windowed_input, std::vector<double>& magnitudes,
                         std::vector<double>& phases) {
  magnitudes.resize(num_bins);
  phases.resize(num_bins);

#ifdef SFERIC_HAS_FFTW3
  std::copy(windowed_input, windowed_input + fft_size, fft_in);
  fftw_execute(plan);

  for (size_t k = 0; k < num_bins; ++k) {
    double re = fft_out[k][0];
    double im = fft_out[k][1];
    magnitudes[k] = std::sqrt(re * re + im * im);
    phases[k] = std::atan2(im, re);
  }
#else
  // Naive DFT fallback (O(N^2) — only for development/testing without FFTW3)
  for (size_t k = 0; k < num_bins; ++k) {
    double re = 0.0;
    double im = 0.0;
    for (size_t n = 0; n < fft_size; ++n) {
      double angle =
          TWO_PI * static_cast<double>(k) * static_cast<double>(n) / static_cast<double>(fft_size);
      re += windowed_input[n] * std::cos(angle);
      im -= windowed_input[n] * std::sin(angle);
    }
    magnitudes[k] = std::sqrt(re * re + im * im);
    phases[k] = std::atan2(im, re);
  }
#endif
}

STFT::STFT(const STFTConfig& config, double sample_rate)
    : config_(config),
      sample_rate_(sample_rate),
      window_(make_window(config.window_type, config.fft_size, config.kaiser_beta)) {
  if (config_.fft_size == 0 || (config_.fft_size & (config_.fft_size - 1)) != 0)
    throw std::invalid_argument("FFT size must be a power of 2");
  if (config_.hop_size == 0 || config_.hop_size > config_.fft_size)
    throw std::invalid_argument("Hop size must be > 0 and <= FFT size");
  if (sample_rate <= 0.0) throw std::invalid_argument("Sample rate must be positive");

  init_fft();
}

STFT::~STFT() = default;
STFT::STFT(STFT&&) noexcept = default;
STFT& STFT::operator=(STFT&&) noexcept = default;

void STFT::init_fft() {
  impl_ = std::make_unique<Impl>();
  impl_->fft_size = config_.fft_size;
  impl_->num_bins = config_.fft_size / 2 + 1;

#ifdef SFERIC_HAS_FFTW3
  impl_->fft_in = fftw_alloc_real(config_.fft_size);
  impl_->fft_out = fftw_alloc_complex(impl_->num_bins);
  impl_->plan = fftw_plan_dft_r2c_1d(static_cast<int>(config_.fft_size), impl_->fft_in,
                                     impl_->fft_out, FFTW_ESTIMATE);
#endif
}

std::vector<SpectralFrame> STFT::analyze(const AudioBuffer& mono_buffer) const {
  if (mono_buffer.num_channels() != 1)
    throw std::invalid_argument("STFT::analyze requires mono input");

  const size_t N = config_.fft_size;
  const size_t hop = config_.hop_size;
  const size_t total_frames = mono_buffer.num_frames();

  if (total_frames < N) return {};

  // Precompute window sum for amplitude normalization
  double window_sum = 0.0;
  for (double w : window_) window_sum += w;
  double norm_general = window_sum / 2.0;  // Bins 1..N/2-1 (split pos/neg freq)
  double norm_dc_nyq = window_sum;         // DC and Nyquist (not split)

  size_t num_hops = (total_frames - N) / hop + 1;
  std::vector<SpectralFrame> result;
  result.reserve(num_hops);

  std::vector<double> windowed(N);

  for (size_t h = 0; h < num_hops; ++h) {
    size_t start = h * hop;
    double center_time = (static_cast<double>(start) + static_cast<double>(N) / 2.0) / sample_rate_;

    // Apply window
    for (size_t i = 0; i < N; ++i) {
      windowed[i] = static_cast<double>(mono_buffer.at(0, start + i)) * window_[i];
    }

    SpectralFrame frame;
    frame.time_seconds = center_time;
    frame.fft_size = N;
    frame.sample_rate = sample_rate_;

    impl_->execute(windowed.data(), frame.magnitudes, frame.phases);

    // Normalize magnitudes so a sine of amplitude A produces a peak of ~A.
    // Raw FFT magnitude for a real sine at amplitude A is A * window_sum / 2
    // (factor of 2: energy splits between positive and negative frequencies).
    for (size_t k = 0; k < frame.magnitudes.size(); ++k) {
      if (k == 0 || k == N / 2) {
        frame.magnitudes[k] /= norm_dc_nyq;
      } else {
        frame.magnitudes[k] /= norm_general;
      }
    }

    result.push_back(std::move(frame));
  }

  return result;
}

}  // namespace analysis
}  // namespace sferic
