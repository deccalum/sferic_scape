#include "analysis/transient_detector.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "analysis/dsp.h"
#include "core/logger.h"

namespace sferic::analysis {

TransientDetector::TransientDetector(TransientDetectorConfig config) : config_(config) {}

std::vector<Detection> TransientDetector::detect(const AudioBuffer& buffer) const {
  SFERIC_SCOPE("TransientDetector::detect");

  const double sr = buffer.sample_rate();
  const size_t frame_len = static_cast<size_t>(config_.frame_ms * 0.001 * sr);
  const size_t hop_len = static_cast<size_t>(config_.hop_ms * 0.001 * sr);
  const double hop_s = static_cast<double>(hop_len) / sr;
  const Sample peak = buffer.peak_amplitude();
  const double ref_db = 20.0 * std::log10(peak > 0.0f ? static_cast<double>(peak) : 1.0);
  std::vector<double> db = rms_envelope_db(buffer, frame_len, hop_len, 1);
  for (double& v : db) v -= ref_db;
  std::vector<Detection> out;
  if (db.size() < 2) return out;
  bool in_candidate = false;
  size_t start_frame = 0;
  double peak_db_in_window = config_.floor_db;
  auto close_candidate = [&](size_t end_frame) {
    const double start_s = static_cast<double>(start_frame) * hop_s;
    const double end_s = static_cast<double>(end_frame) * hop_s;
    if (end_s - start_s < config_.min_duration_s) return;
    const double confidence =
        std::clamp((peak_db_in_window - config_.floor_db) / (0.0 - config_.floor_db), 0.0, 1.0);
    if (!out.empty() && start_s - out.back().window.end_s < config_.min_gap_s) {
      out.back().window.end_s = end_s;
      out.back().window.end_frame = static_cast<size_t>(end_s * sr);
      out.back().window.confidence = std::max(out.back().window.confidence, confidence);
    } else {
      out.push_back(Detection{TimeWindow{start_s, end_s, static_cast<size_t>(start_s * sr),
                                         static_cast<size_t>(end_s * sr), confidence},
                              {}});
    }
  };
  for (size_t i = 1; i < db.size(); ++i) {
    const double flux = db[i] - db[i - 1];
    if (!in_candidate && flux > config_.onset_threshold_db) {
      in_candidate = true;
      start_frame = i - 1;
      peak_db_in_window = db[i];
      continue;
    }
    if (in_candidate) {
      peak_db_in_window = std::max(peak_db_in_window, db[i]);
      const double elapsed_s = static_cast<double>(i - start_frame) * hop_s;
      if (db[i] < config_.floor_db || elapsed_s > config_.max_duration_s) {
        close_candidate(i);
        in_candidate = false;
      }
    }
  }
  if (in_candidate) close_candidate(db.size() - 1);

  SFERIC_LOG(Info, "found " + std::to_string(out.size()) + " transient candidates");
  return out;
}

}  // namespace sferic::analysis
