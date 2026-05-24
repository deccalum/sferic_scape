#pragma once

#include <vector>

#include "analysis/stft.h"
#include "core/buffer.h"
#include "core/types.h"

namespace sferic {
namespace analysis {

// A band from one resolution pass, covering a frequency range.
struct ResolutionBand {
  size_t fft_size;
  double freq_lo;          // Hz (inclusive)
  double freq_hi;          // Hz (exclusive, or Nyquist)
  double freq_resolution;  // Hz per bin
  double time_resolution;  // seconds per hop
};

// Result of multi-resolution analysis: spectral frames with mixed resolution.
// Each frame contains the best-resolution data for each frequency region.
struct MultiResFrame {
  double time_seconds;
  // Per-bin data merged from multiple resolutions
  std::vector<double> frequencies;  // Center frequency of each bin (Hz)
  std::vector<double> magnitudes;   // Magnitude at each bin
  std::vector<double> phases;       // Phase at each bin
};

struct MultiResConfig {
  // Each STFT pass: { fft_size, hop_size, window_type }
  // Default: long window for low freq, medium for mid, short for high freq
  struct Pass {
    STFTConfig stft;
    double freq_lo;  // This pass is authoritative for freqs >= freq_lo
    double freq_hi;  // ... and < freq_hi
  };

  std::vector<Pass> passes;
  WindowType window_type = WindowType::HANN;

  // Create a default 3-band config for a given sample rate
  static MultiResConfig default_config(double sample_rate);
};

class MultiResolutionSTFT {
 public:
  explicit MultiResolutionSTFT(const MultiResConfig& config, double sample_rate);

  // Analyze a mono buffer. Returns frames at the time resolution of the
  // shortest window (finest time resolution).
  std::vector<MultiResFrame> analyze(const AudioBuffer& mono_buffer) const;

  // Get info about each resolution band
  const std::vector<ResolutionBand>& bands() const {
    return bands_;
  }

  double sample_rate() const {
    return sample_rate_;
  }

 private:
  MultiResConfig config_;
  double sample_rate_;
  std::vector<STFT> stfts_;
  std::vector<ResolutionBand> bands_;
};

}  // namespace analysis
}  // namespace sferic
