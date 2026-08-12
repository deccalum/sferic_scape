#pragma once

#include <string_view>
#include <vector>

#include "analysis/detect.h"
#include "core/buffer.h"

namespace sferic::analysis {

inline constexpr const char* kTransientDetectorName = "broadband_transient";
inline constexpr const char* kTransientDetectorVersion = "v1";

struct TransientDetectorConfig {
  double frame_ms = 20.0;           // analysis frame length
  double hop_ms = 10.0;             // hop between frames
  double onset_threshold_db = 6.0;  // dB rise per hop that opens a candidate
  double floor_db = -50.0;          // dB, relative to buffer peak, that closes one
  double min_duration_s = 0.05;     // shortest candidate worth keeping
  double max_duration_s = 8.0;      // longest window before force-closing
  double min_gap_s = 0.2;           // candidates closer than this are merged
};

// Broadband energy-flux onset detector.
// walks samples to produce clips for review
class TransientDetector final : public IDetector {
 public:
  explicit TransientDetector(TransientDetectorConfig config = {});
  std::string_view name() const override { return kTransientDetectorName; }
  std::string_view version() const override { return kTransientDetectorVersion; }
  std::vector<Detection> detect(const AudioBuffer& buffer) const override;

 private:
  TransientDetectorConfig config_;
};

}  // namespace sferic::analysis
