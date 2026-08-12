#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include "core/buffer.h"

namespace sferic::analysis {

// one event window.
// domain detectors attach richer payloads via their own types.
// this seam only carries the timed window + a 0–1 score.
struct TimeWindow {
  double start_s;      // window start, seconds into the clip
  double end_s;        // window end, seconds into the clip
  size_t start_frame;  // sample index ≈ start_s * sr
  size_t end_frame;    // sample index ≈ end_s * sr
  double confidence;   // domain-defined score in [0, 1]
};

// Intersection over union of `w` and [start_s, end_s) — 1.0 when the two windows
// are identical, 0.0 when they do not overlap. The measure of "is this the same
// event" when one detector version's output is compared against another's.
constexpr double iou(const TimeWindow& w, double start_s, double end_s) {
  const double inter = std::min(w.end_s, end_s) - std::max(w.start_s, start_s);
  if (inter <= 0.0) return 0.0;
  const double uni = std::max(w.end_s, end_s) - std::min(w.start_s, start_s);
  return uni > 0.0 ? inter / uni : 0.0;
}

// One analysis frame of a detection's internal evolution — compact band and
// stereo scalars, not raw FFT bins. Field-for-field the `exemplar_feature`
// table; the CLI maps it to io::FeatureFrameRow at the DB boundary. Which band
// "low_band" refers to is the emitting detector's business.
struct FeatureFrame {
  double t_s;              // time from window start
  double low_band_db;      // sub-band energy, dB
  double centroid_hz;      // sub-band spectral centroid
  double spread_hz;        // sub-band spectral spread
  double dominance;        // sub-band / total energy fraction
  double stereo_corr;      // interaural L/R correlation at lag 0
  double ms_width;         // mid/side width — side / (mid + side)
  double phase_coherence;  // max L/R correlation over ITD lags
  double stereo_balance;   // R / (L + R) energy — 0.5 centred
};

struct Detection {
  TimeWindow window;                   // timed cut + confidence
  std::vector<FeatureFrame> features;  // empty when the detector emits windows only
};

struct IDetector {
  virtual ~IDetector() = default;
  virtual std::string_view name() const = 0;
  virtual std::string_view version() const = 0;
  virtual std::vector<Detection> detect(const AudioBuffer& buffer) const = 0;
};

}  // namespace sferic::analysis
