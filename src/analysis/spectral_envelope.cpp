#include "analysis/spectral_envelope.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#include "core/constants.h"
#include "core/logger.h"

namespace sferic::analysis {

struct SpectralAnalyzer::Impl {
  size_t fft_size;             // FFT length (power of 2)
  size_t num_bins;             // real bins kept: fft_size/2 + 1
  std::vector<double> window;  // analysis window, length fft_size
  double norm_general;         // bins 1..N/2-1 — split between positive and negative frequencies
  double norm_dc_nyq;          // DC and Nyquist — not split

  fftw_plan plan = nullptr;       // real→complex plan
  double* fft_in = nullptr;       // windowed real input buffer
  fftw_complex* fft_out = nullptr;  // complex spectrum output

  ~Impl() {
    if (plan) fftw_destroy_plan(plan);
    if (fft_in) fftw_free(fft_in);
    if (fft_out) fftw_free(fft_out);
  }

  void execute(const double* windowed_input, std::vector<double>& magnitudes) {
    std::copy(windowed_input, windowed_input + fft_size, fft_in);
    fftw_execute(plan);
    magnitudes.resize(num_bins);
    for (size_t k = 0; k < num_bins; ++k) {
      const double re = fft_out[k][0];
      const double im = fft_out[k][1];
      const double norm = (k == 0 || k == fft_size / 2) ? norm_dc_nyq : norm_general;
      magnitudes[k] = std::sqrt(re * re + im * im) / norm;
    }
  }
};

SpectralAnalyzer::SpectralAnalyzer(size_t fft_size, size_t hop_size, size_t smoothing_bins)
    : fft_size_(fft_size),
      hop_size_(hop_size),
      smoothing_bins_(smoothing_bins),
      impl_(std::make_unique<Impl>()) {
  impl_->fft_size = fft_size_;
  impl_->num_bins = fft_size_ / 2 + 1;
  impl_->window.resize(fft_size_);
  for (size_t i = 0; i < fft_size_; ++i)
    impl_->window[i] =
        0.5 *
        (1.0 - std::cos(TWO_PI * static_cast<double>(i) / static_cast<double>(fft_size_ - 1)));

  double window_sum = 0.0;
  for (double w : impl_->window) window_sum += w;
  impl_->norm_general = window_sum / 2.0;
  impl_->norm_dc_nyq = window_sum;

  impl_->fft_in = fftw_alloc_real(fft_size_);
  impl_->fft_out = fftw_alloc_complex(impl_->num_bins);
  impl_->plan = fftw_plan_dft_r2c_1d(static_cast<int>(fft_size_), impl_->fft_in, impl_->fft_out,
                                     FFTW_ESTIMATE);

  std::ostringstream ss;
  ss << "fft=" << fft_size_
     << " hop=" << (hop_size_ == 0 ? std::string("auto(1ms)") : std::to_string(hop_size_))
     << " smoothing=" << smoothing_bins_;
  SFERIC_LOG(Info, ss.str());
}

SpectralAnalyzer::~SpectralAnalyzer() = default;
SpectralAnalyzer::SpectralAnalyzer(SpectralAnalyzer&&) noexcept = default;
SpectralAnalyzer& SpectralAnalyzer::operator=(SpectralAnalyzer&&) noexcept = default;

std::vector<double> SpectralAnalyzer::smooth_envelope(const std::vector<double>& magnitudes) const {
  const size_t n = magnitudes.size();
  const int half = static_cast<int>(smoothing_bins_ / 2);
  std::vector<double> smoothed(n);

  for (size_t i = 0; i < n; ++i) {
    double sum = 0.0;
    int count = 0;
    const int lo = std::max(0, static_cast<int>(i) - half);
    const int hi = std::min(static_cast<int>(n) - 1, static_cast<int>(i) + half);
    for (int j = lo; j <= hi; ++j) {
      sum += magnitudes[static_cast<size_t>(j)];
      ++count;
    }
    smoothed[i] = (count > 0) ? sum / static_cast<double>(count) : 0.0;
  }

  return smoothed;
}

size_t SpectralAnalyzer::frame_count(const AudioBuffer& residual) const {
  const size_t hop = (hop_size_ == 0)
                         ? static_cast<size_t>(std::round(residual.sample_rate() / 1000.0))
                         : hop_size_;
  const size_t total = residual.num_frames();
  return total < fft_size_ ? 0 : (total - fft_size_) / hop + 1;
}

void SpectralAnalyzer::analyze_streaming(const AudioBuffer& residual,
                                         const std::function<void(EnvelopeFrame&&)>& sink) const {
  SFERIC_SCOPE("SpectralAnalyzer::analyze_streaming");

  const AudioBuffer mono = (residual.num_channels() == 1) ? residual : residual.to_mono();
  const double sample_rate = mono.sample_rate();
  const size_t effective_hop =
      (hop_size_ == 0) ? static_cast<size_t>(std::round(sample_rate / 1000.0)) : hop_size_;
  const double duration_s = static_cast<double>(mono.num_frames()) / sample_rate;
  const double ms_per_frame = static_cast<double>(effective_hop) / sample_rate * 1000.0;
  const size_t expected_ms = static_cast<size_t>(duration_s * 1000.0 / ms_per_frame);

  {
    std::ostringstream ss;
    ss << "channels=" << residual.num_channels()
       << (residual.num_channels() != 1 ? " (converted to mono)" : "")
       << "  sample_rate=" << sample_rate << "Hz"
       << "  hop=" << effective_hop << " (" << std::fixed << std::setprecision(2) << ms_per_frame
       << "ms)"
       << "  input=" << duration_s << "s (~" << expected_ms << " ms-frames)";
    SFERIC_LOG(Info, ss.str());
  }

  const size_t N = fft_size_;
  const size_t num_hops = frame_count(mono);
  const double freq_spacing = sample_rate / static_cast<double>(N);
  const size_t n_bins = impl_->num_bins;
  const size_t frame_bytes = n_bins * sizeof(double);

  {
    std::ostringstream ss;
    ss << "STFT (streamed): " << num_hops << " frames, " << n_bins << " bins/frame, "
       << log::fmt_bytes(frame_bytes) << "/frame — reduced per frame, not stored";
    SFERIC_LOG(Info, ss.str());
  }

  std::vector<double> windowed(N);
  std::vector<double> magnitudes;
  EnvelopeFrame sf;  // reused across frames — O(1) storage
  sf.frequency_spacing = freq_spacing;

  for (size_t h = 0; h < num_hops; ++h) {
    const size_t start = h * effective_hop;
    const double center_time =
        (static_cast<double>(start) + static_cast<double>(N) / 2.0) / sample_rate;

    for (size_t i = 0; i < N; ++i)
      windowed[i] = static_cast<double>(mono.at(0, start + i)) * impl_->window[i];

    impl_->execute(windowed.data(), magnitudes);

    sf.time_seconds = center_time;
    sf.envelope = smooth_envelope(magnitudes);
    sink(std::move(sf));

    SFERIC_PROGRESS(h + 1, num_hops, center_time * 1000.0, (h + 1) * frame_bytes);
  }

  SFERIC_LOG(Info, "streamed " + std::to_string(num_hops) + " frames");
}

SpectralEnvelope SpectralAnalyzer::analyze(const AudioBuffer& residual) const {
  SpectralEnvelope model;
  model.sample_rate = residual.sample_rate();
  model.ms.reserve(frame_count(residual));
  analyze_streaming(residual, [&](EnvelopeFrame&& frame) {
    model.ms.push_back(std::move(frame));
  });
  SFERIC_LOG_MEM(Info, "collected " + std::to_string(model.ms.size()) + " ms-frames",
                 model.size_bytes());
  return model;
}

}  // namespace sferic::analysis
