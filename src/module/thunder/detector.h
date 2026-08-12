#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "analysis/detect.h"
#include "analysis/dsp.h"
#include "core/buffer.h"

namespace sferic::thunder {

// Ledger identity for this implementation.
// !Bump kDetectorVersion when rumble / feature semantics change
// so media_detection reprocesses the corpus.
inline constexpr const char* kDetectorName = "thunder_onset_rumble";

// v11 — unbiased envelope edges, floor survives short clips, reject bar 3.0->3.8
inline constexpr const char* kDetectorVersion = "v11";

enum class RumbleEnd { Gate, Level, Cap };

// One thunder-band frame of the rumble pass.
// raw per-frame measurements measure_rumble reduces into a RumbleScore.
struct RumbleFrame {
  double time_s;       // frame time in the clip, seconds
  double low_energy;   // thunder-band energy this frame
  double tot_energy;   // full-band energy this frame
  double centroid_hz;  // low-band spectral centroid
  double spread_hz;    // low-band spectral spread
};

struct DetectorConfig {
  analysis::OnsetConfig onset;      // clap attack detection — see analysis/dsp.h
  double release_s = 3.0;           // time below the gate before the rumble closes
  double arm_drop = 0.9;            // fraction of the peak -> floor span that arms the release
  double settle_s = 2.0;            // slope window at peak level
  double settle_min_s = 0.5;        // slope window at the floor
  double level_slope = 1.0;         // dB/s — |slope| below this reads as a plateau
  double min_prominence = 0.0;      // attack gate as a fraction of the recording's dynamic range
  double env_hop_s = 0.010;         // broadband envelope frame hop
  double env_smooth_s = 0.050;      // broadband envelope moving-average window
  double peak_search_win_s = 1.0;   // window after onset a local peak dB is searched over
  double pre_attack_guard_s = 0.1;  // gap before the attack excluded from its own pre-attack floor
  double max_itd_s = 0.0007;        // max interaural time difference searched for stereo coherence
  double rumble_hop_s = 0.020;      // rumble (long-FFT) pass frame hop
  size_t rumble_fft = 8192;         // rumble pass FFT size
  double reject_gap_factor = 2.0;   // gap_valley_threshold: valley must dominate the median by this
  double reject_min_significance = 3.8;  // otsu_threshold: confidence must clear this bar to reject
};

// What measure_rumble found following one attack.
struct RumbleScore {
  double end_s = 0.0;                              // rumble close time, seconds into the clip
  double confidence = 0.0;                         // blended ring + drift + dominance score [0, 1]
  double rumble_duration_s = 0.0;                  // end_s − attack onset
  double drift_score = 0.0;                        // downward centroid drift component of confidence
  double dominance = 0.0;                          // low-band / total energy fraction
  double prominence = 0.0;                         // peak rise above pre-attack floor, dB
  RumbleEnd close = RumbleEnd::Gate;               // which path closed the ring
  double stereo_invariance = 0.0;                  // stereo stability across the ring (0 until used)
  std::vector<analysis::FeatureFrame> features;    // per-frame series for exemplar_feature
};

class ThunderDetector final : public analysis::IDetector {
 public:
  explicit ThunderDetector(DetectorConfig config = {});
  std::string_view name() const override { return kDetectorName; }
  std::string_view version() const override { return kDetectorVersion; }
  std::vector<analysis::Detection> detect(const AudioBuffer& buffer) const override;

 private:
  DetectorConfig config_;
};

}  // namespace sferic::thunder
