#include "module/thunder/detector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/dsp.h"
#include "analysis/spectral_envelope.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "module/thunder/constants.h"

namespace sferic::thunder {
namespace {

using analysis::Detection;
using analysis::EnvelopeFrame;
using analysis::FeatureFrame;
using analysis::SpectralAnalyzer;
using analysis::StereoFrame;
using analysis::TimeWindow;

constexpr std::string_view to_string(RumbleEnd e) {
  switch (e) {
    case RumbleEnd::Gate:
      return "gate";
    case RumbleEnd::Level:
      return "level";
    case RumbleEnd::Cap:
      return "cap";
  }
  std::unreachable();
}

// Reduce one STFT frame to the thunder band — energy below FUNDAMENTAL_CEIL_HZ
// plus that band's centroid and spread, which carry the downward-drift cue.
RumbleFrame rumble_frame_of(const EnvelopeFrame& f) {
  const size_t hi_bin = std::min(
      f.envelope.size(), static_cast<size_t>(FUNDAMENTAL_CEIL_HZ / f.frequency_spacing) + 1);
  double low_e = 0.0;
  double tot_e = 0.0;
  double cnum = 0.0;
  double snum = 0.0;
  double cden = 0.0;
  for (size_t b = 0; b < f.envelope.size(); ++b) {
    const double e = f.envelope[b] * f.envelope[b];
    tot_e += e;
    if (b < hi_bin) {
      const double freq = static_cast<double>(b) * f.frequency_spacing;
      low_e += e;
      cnum += freq * f.envelope[b];
      snum += freq * freq * f.envelope[b];
      cden += f.envelope[b];
    }
  }
  const double centroid = cden > 0.0 ? cnum / cden : 0.0;
  const double spread =
      cden > 0.0 ? std::sqrt(std::max(0.0, snum / cden - centroid * centroid)) : 0.0;
  return RumbleFrame{f.time_seconds, low_e, tot_e, centroid, spread};
}

// First rumble frame at or after `t`, clamped into range.
size_t rumble_index_at(const std::vector<RumbleFrame>& rumble, double t) {
  size_t i = 0;
  while (i < rumble.size() && rumble[i].time_s < t) ++i;
  return std::min(i, rumble.empty() ? size_t{0} : rumble.size() - 1);
}

// Measure the rumble following one attack.
// rumble holds open from its onset frame and closes at whichever fires first.
//
// When no gate exists (a lone clap), the floor is this onset's own pre-attack
// floor. The window bounds are then mapped onto the thunder-band `rumble`
// frames for the per-frame feature capture, drift and dominance. Confidence
// stays the equal-weight mean of the three physically-normalised cues (0–1)
RumbleScore measure_rumble(const std::vector<double>& env_db, double env_hop_s, size_t onset_frame,
                           std::optional<double> gate_db, const DetectorConfig& cfg,
                           const std::vector<RumbleFrame>& rumble,
                           const std::vector<StereoFrame>& stereo) {
  RumbleScore rs;
  const double onset_s = static_cast<double>(onset_frame) * env_hop_s;
  rs.end_s = onset_s;
  if (onset_frame >= env_db.size()) return rs;
  const double floor_ref =
      gate_db ? *gate_db
              : analysis::local_median_floor(env_db, onset_frame, env_hop_s, cfg.onset.lookback_s,
                                             cfg.pre_attack_guard_s);
  const size_t peak_win =
      std::max<size_t>(1, static_cast<size_t>(std::round(cfg.peak_search_win_s / env_hop_s)));
  double peak_db = env_db[onset_frame];
  for (size_t i = onset_frame; i < std::min(env_db.size(), onset_frame + peak_win); ++i)
    peak_db = std::max(peak_db, env_db[i]);
  const double span = std::max(1e-6, peak_db - floor_ref);
  const size_t release =
      std::max<size_t>(1, static_cast<size_t>(std::round(cfg.release_s / env_hop_s)));
  const size_t cap_frame = std::min(
      env_db.size(), onset_frame + static_cast<size_t>(std::round(RUMBLE_MAX_S / env_hop_s)));
  size_t end = cap_frame > onset_frame ? cap_frame - 1 : onset_frame;
  RumbleEnd close = RumbleEnd::Cap;
  size_t below = 0;
  size_t drop_start = onset_frame;
  std::optional<size_t> armed;
  for (size_t f = onset_frame; f < cap_frame; ++f) {
    if (gate_db) {
      if (env_db[f] < *gate_db) {
        if (below == 0) drop_start = f;
        ++below;
        if (below >= release) {
          end = drop_start;
          close = RumbleEnd::Gate;
          break;
        }
      } else {
        below = 0;
      }
    }
    if (!armed && (peak_db - env_db[f]) >= cfg.arm_drop * span) armed = f;
    if (armed) {
      const double level_frac = std::clamp((env_db[f] - floor_ref) / span, 0.0, 1.0);
      const double settle = cfg.settle_min_s + level_frac * (cfg.settle_s - cfg.settle_min_s);
      const size_t win = std::max<size_t>(2, static_cast<size_t>(std::round(settle / env_hop_s)));
      const std::span<const double> tail(env_db.data() + f, std::min(win, env_db.size() - f));
      const double s = analysis::ols_slope_uniform(tail, env_hop_s);
      if (-cfg.level_slope < s && s < cfg.level_slope) {
        end = f;
        close = RumbleEnd::Level;
        break;
      }
    }
  }
  const double end_s = static_cast<double>(end) * env_hop_s;
  const double rumble_duration = end_s - onset_s;
  const size_t rs0 = rumble_index_at(rumble, onset_s);
  const size_t rs1 = std::max(rs0, rumble_index_at(rumble, end_s));
  std::vector<double> times;
  std::vector<double> centroids;
  for (size_t i = rs0; i <= rs1 && i < rumble.size(); ++i) {
    times.push_back(rumble[i].time_s);
    centroids.push_back(rumble[i].centroid_hz);
  }
  const size_t n = times.size();
  const double slope = analysis::ols_slope(times, centroids);
  double centroid_mean = 0.0;
  if (n >= 2) {
    for (const double c : centroids) centroid_mean += c;
    centroid_mean /= static_cast<double>(n);
  }
  double dominance_sum = 0.0;
  size_t dom_n = 0;
  for (size_t i = rs0; i <= rs1 && i < rumble.size(); ++i) {
    if (rumble[i].tot_energy > 0.0) {
      dominance_sum += rumble[i].low_energy / rumble[i].tot_energy;
      ++dom_n;
    }
  }
  const double duration_score = std::clamp(rumble_duration / RUMBLE_MIN_S, 0.0, 1.0);
  const double drift_score = (slope < 0.0 && centroid_mean > 0.0)
                                 ? std::clamp(-slope * rumble_duration / centroid_mean, 0.0, 1.0)
                                 : 0.0;
  const double dominance = dom_n ? dominance_sum / static_cast<double>(dom_n) : 0.0;
  rs.end_s = std::max(end_s, onset_s + CLAP_MIN_S);
  rs.rumble_duration_s = rumble_duration;
  rs.drift_score = drift_score;
  rs.dominance = dominance;
  rs.prominence = peak_db - floor_ref;
  rs.close = close;
  rs.confidence = (duration_score + drift_score + dominance) / 3.0;
  double corr_sum = 0.0;
  for (size_t i = rs0; i <= rs1 && i < rumble.size(); ++i) {
    rs.features.push_back(
        FeatureFrame{rumble[i].time_s, 10.0 * std::log10(rumble[i].low_energy + 1e-12),
                     rumble[i].centroid_hz, rumble[i].spread_hz,
                     rumble[i].tot_energy > 0.0 ? rumble[i].low_energy / rumble[i].tot_energy : 0.0,
                     stereo[i].corr, stereo[i].width, stereo[i].coherence, stereo[i].balance});
    corr_sum += stereo[i].corr;
  }
  rs.stereo_invariance = n ? corr_sum / static_cast<double>(n) : 1.0;
  return rs;
}

}  // namespace

ThunderDetector::ThunderDetector(DetectorConfig config) : config_(config) {}

std::vector<Detection> ThunderDetector::detect(const AudioBuffer& buffer) const {
  SFERIC_SCOPE("ThunderDetector::detect");
  const double sr = buffer.sample_rate();
  const size_t env_hop =
      std::max<size_t>(1, static_cast<size_t>(std::round(sr * config_.env_hop_s)));
  const double env_hop_s = static_cast<double>(env_hop) / sr;
  const size_t env_smooth = std::max<size_t>(
      1, static_cast<size_t>(std::round(config_.env_smooth_s / config_.env_hop_s)));
  const std::vector<double> env_db =
      analysis::rms_envelope_db(buffer, env_hop, env_hop, env_smooth);
  const std::vector<size_t> attack_frames =
      analysis::detect_onsets(env_db, env_hop_s, config_.onset);
  const size_t rumble_hop =
      std::max<size_t>(1, static_cast<size_t>(std::round(sr * config_.rumble_hop_s)));
  std::vector<RumbleFrame> rumble;
  SpectralAnalyzer(config_.rumble_fft, rumble_hop, 4)
      .analyze_streaming(buffer, [&](const EnvelopeFrame& f) {
        rumble.push_back(rumble_frame_of(f));
      });
  std::vector<double> rumble_times;
  rumble_times.reserve(rumble.size());
  for (const RumbleFrame& f : rumble) rumble_times.push_back(f.time_s);
  const std::vector<StereoFrame> stereo =
      analysis::stereo_frames(buffer, rumble_times, rumble_hop, config_.max_itd_s);
  const std::optional<double> gate_db =
      (attack_frames.size() < 2 || env_db.empty())
          ? std::nullopt
          : std::optional<double>(analysis::quiet_half_mean(env_db));
  std::vector<size_t> attacks;
  attacks.reserve(attack_frames.size());
  {
    const double dyn = config_.min_prominence > 0.0
                           ? analysis::percentile(env_db, 0.99) - analysis::percentile(env_db, 0.05)
                           : 0.0;
    const double bar = config_.min_prominence * dyn;
    const size_t peak_win =
        std::max<size_t>(1, static_cast<size_t>(std::round(config_.peak_search_win_s / env_hop_s)));
    for (const size_t a : attack_frames) {
      if (config_.min_prominence <= 0.0) {
        attacks.push_back(a);
        continue;
      }
      const double fl = analysis::local_median_floor(env_db, a, env_hop_s, config_.onset.lookback_s,
                                                     config_.pre_attack_guard_s);
      double peak = env_db[a];
      for (size_t i = a; i < std::min(env_db.size(), a + peak_win); ++i)
        peak = std::max(peak, env_db[i]);
      if (peak - fl >= bar) attacks.push_back(a);
    }
  }
  std::vector<RumbleScore> scored;
  std::vector<double> onset_of;
  std::vector<double> ratios;
  std::vector<double> confidences;
  for (size_t k = 0; k < attacks.size();) {
    const double onset_s = static_cast<double>(attacks[k]) * env_hop_s;
    RumbleScore rs =
        measure_rumble(env_db, env_hop_s, attacks[k], gate_db, config_, rumble, stereo);
    ratios.push_back(rs.prominence > 0.0 ? rs.rumble_duration_s / rs.prominence : 0.0);
    confidences.push_back(rs.confidence);
    const double end_s = rs.end_s;
    onset_of.push_back(onset_s);
    scored.push_back(std::move(rs));
    ++k;
    while (k < attacks.size() && static_cast<double>(attacks[k]) * env_hop_s <= end_s) ++k;
  }
  const double reject_thr = analysis::gap_valley_threshold(ratios, config_.reject_gap_factor);
  const double conf_reject_thr =
      analysis::otsu_threshold(confidences, config_.reject_min_significance);
  std::vector<Detection> detections;
  detections.reserve(scored.size());
  for (size_t i = 0; i < scored.size(); ++i) {
    RumbleScore& rs = scored[i];
    const double onset_s = onset_of[i];
    const bool rumble_reject = ratios[i] <= 0.0 || ratios[i] < reject_thr;
    const bool conf_reject = rs.confidence < conf_reject_thr;
    if (rumble_reject || conf_reject) {
      SFERIC_LOG(Info, "reject onset " + std::to_string(onset_s) + "s —" +
                           (rumble_reject ? " rumble/prom " + std::to_string(ratios[i]) + " < " +
                                                std::to_string(reject_thr)
                                          : "") +
                           (conf_reject ? " confidence " + std::to_string(rs.confidence) + " < " +
                                              std::to_string(conf_reject_thr)
                                        : ""));
      continue;
    }
    const TimeWindow w{onset_s, rs.end_s, static_cast<size_t>(onset_s * sr),
                       static_cast<size_t>(rs.end_s * sr), rs.confidence};
    detections.push_back(Detection{w, std::move(rs.features)});
    SFERIC_LOG(Info, "onset " + std::to_string(onset_s) + "s -> rumble " +
                         std::to_string(rs.rumble_duration_s) + "s [" +
                         std::string(to_string(rs.close)) + "], drift " +
                         std::to_string(rs.drift_score) + ", dom " + std::to_string(rs.dominance) +
                         ", prom " + std::to_string(rs.prominence) + ", stereo-inv " +
                         std::to_string(rs.stereo_invariance) + " -> conf " +
                         std::to_string(rs.confidence));
  }

  SFERIC_LOG(Info, "detected " + std::to_string(detections.size()) + " thunder candidates from " +
                       std::to_string(attack_frames.size()) + " attacks (" +
                       std::to_string(attacks.size()) + " after prominence gate, gate " +
                       (gate_db ? std::to_string(*gate_db) + " dB" : std::string("disabled")) +
                       ")");
  return detections;
}

}  // namespace sferic::thunder
