#pragma once

#include <memory>
#include <vector>

#include "analysis/window.h"
#include "core/buffer.h"
#include "core/types.h"

namespace sferic {
namespace analysis {

struct STFTConfig {
  size_t fft_size = 4096;
  size_t hop_size = 1024;  // default 75% overlap
  WindowType window_type = WindowType::HANN;
  double kaiser_beta = 8.0;  // only used if window_type == KAISER
};

class STFT {
 public:
  explicit STFT(const STFTConfig& config, double sample_rate);
  ~STFT();

  // Non-copyable, movable
  STFT(const STFT&) = delete;
  STFT& operator=(const STFT&) = delete;
  STFT(STFT&&) noexcept;
  STFT& operator=(STFT&&) noexcept;

  // Analyze a mono audio buffer into a sequence of spectral frames.
  // Input must be mono (1 channel). Use AudioBuffer::to_mono() first if needed.
  std::vector<SpectralFrame> analyze(const AudioBuffer& mono_buffer) const;

  // Getters
  size_t fft_size() const {
    return config_.fft_size;
  }
  size_t hop_size() const {
    return config_.hop_size;
  }
  double sample_rate() const {
    return sample_rate_;
  }
  size_t num_bins() const {
    return config_.fft_size / 2 + 1;
  }

  // Frequency resolution (Hz per bin)
  double frequency_resolution() const {
    return sample_rate_ / static_cast<double>(config_.fft_size);
  }

  // Time resolution (seconds per hop)
  double time_resolution() const {
    return static_cast<double>(config_.hop_size) / sample_rate_;
  }

 private:
  STFTConfig config_;
  double sample_rate_;
  std::vector<double> window_;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  void init_fft();
};

}  // namespace analysis
}  // namespace sferic
