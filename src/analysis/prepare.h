#pragma once

#include <optional>

#include "analysis/dsp.h"
#include "core/buffer.h"

namespace sferic::analysis::prepare {

enum class MonoPolicy { Average };

struct PrepareConfig {
  double target_peak = 0.9;               // peak |sample| after normalize
  bool extract_stereo = true;             // capture StereoProfile before mono collapse
  MonoPolicy mono = MonoPolicy::Average;  // channel collapse policy
  double max_itd_s = 0.0007;              // lag bound for the stereo profile's coherence search
};

// Result of materialize: a level-consistent mono clip plus optional stereo
// summary captured before the mono collapse.
struct PreparedClip {
  AudioBuffer mono;                     // peak-normalized mono slice
  double gain_applied;                  // linear scale applied by normalize_peak
  std::optional<StereoProfile> stereo;  // cues from the multichannel cut (if extract_stereo)
  double start_s;                       // slice start in the source, seconds
  double end_s;                         // slice end in the source, seconds
};

// Slice [start_s, end_s) from `full` (all channels preserved).
AudioBuffer cut(const AudioBuffer& full, double start_s, double end_s);

// Collapse to mono.
AudioBuffer to_mono(const AudioBuffer& buffer, MonoPolicy policy = MonoPolicy::Average);

// Scale so peak absolute sample equals `target_peak`. Silent buffers unchanged.
// `gain_out` receives the linear scale applied when non-null.
AudioBuffer normalize_peak(const AudioBuffer& buffer, double target_peak = 0.9,
                           double* gain_out = nullptr);

// cut -> stereo profile (on the multichannel cut) -> mono -> peak normalize.
// The stereo summary itself is analysis::extract_stereo_profile (analysis/dsp.h).
PreparedClip materialize(const AudioBuffer& full, double start_s, double end_s,
                         PrepareConfig cfg = {});

}  // namespace sferic::analysis::prepare
