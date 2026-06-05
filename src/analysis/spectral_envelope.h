#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "core/buffer.h"

namespace sferic {
namespace analysis {

// Spectral envelope snapshot at one point in time (~1ms per frame at default hop).
struct EnvelopeFrame {
  double time_seconds;
  std::vector<double> envelope;  // magnitude per FFT bin, 0 Hz → Nyquist
  double frequency_spacing;      // Hz between adjacent bins
};

// Spectral envelope model: a time series of per-frame spectral envelopes.
// Produced by SpectralAnalyzer; consumed by ParametricExtractor and NoiseGenerator.
struct SpectralEnvelope {
  double sample_rate;
  std::vector<EnvelopeFrame> ms;  // named 'ms' — each entry ≈ 1ms of audio

  bool empty() const { return ms.empty(); }
  size_t num_bins() const { return ms.empty() ? 0 : ms.front().envelope.size(); }
  double duration() const {
    if (ms.empty()) return 0.0;
    return ms.back().time_seconds - ms.front().time_seconds;
  }
  size_t size_bytes() const {
    size_t total = 0;
    for (const auto& f : ms) total += f.envelope.size() * sizeof(double);
    return total;
  }
};

// Extracts a SpectralEnvelope from an AudioBuffer via windowed STFT.
//   fft_size       — frequency resolution (must be power of 2)
//   hop_size       — frames between hops; 0 = auto (~1ms per frame)
//   smoothing_bins — moving-average half-width for envelope smoothing (empirical)
class SpectralAnalyzer {
 public:
  SpectralAnalyzer(size_t fft_size, size_t hop_size, size_t smoothing_bins);
  ~SpectralAnalyzer();
  SpectralAnalyzer(const SpectralAnalyzer&) = delete;
  SpectralAnalyzer& operator=(const SpectralAnalyzer&) = delete;
  SpectralAnalyzer(SpectralAnalyzer&&) noexcept;
  SpectralAnalyzer& operator=(SpectralAnalyzer&&) noexcept;

  // Sample rate is taken from residual.sample_rate().
  SpectralEnvelope analyze(const AudioBuffer& residual) const;

 private:
  size_t fft_size_;
  size_t hop_size_;
  size_t smoothing_bins_;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  std::vector<double> smooth_envelope(const std::vector<double>& magnitudes) const;
};

}  // namespace analysis
}  // namespace sferic
