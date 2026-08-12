#include "analysis/prepare.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "core/logger.h"

namespace sferic::analysis::prepare {

AudioBuffer cut(const AudioBuffer& full, double start_s, double end_s) {
  const double sr = full.sample_rate();
  const size_t start = static_cast<size_t>(start_s * sr);
  const size_t end = std::min(static_cast<size_t>(end_s * sr), full.num_frames());
  const size_t n = end > start ? end - start : 0;
  AudioBuffer seg(full.num_channels(), n, sr);
  for (size_t c = 0; c < full.num_channels(); ++c)
    for (size_t f = 0; f < n; ++f) seg.at(c, f) = full.at(c, start + f);
  return seg;
}

AudioBuffer to_mono(const AudioBuffer& buffer, MonoPolicy) { return buffer.to_mono(); }

AudioBuffer normalize_peak(const AudioBuffer& buffer, double target_peak, double* gain_out) {
  const Sample peak = buffer.peak_amplitude();
  if (buffer.empty() || peak <= 0.0f || target_peak <= 0.0) {
    if (gain_out) *gain_out = 1.0;
    return buffer;
  }
  const double gain = target_peak / static_cast<double>(peak);
  if (gain_out) *gain_out = gain;
  AudioBuffer out(buffer.num_channels(), buffer.num_frames(), buffer.sample_rate());
  for (size_t c = 0; c < buffer.num_channels(); ++c)
    for (size_t f = 0; f < buffer.num_frames(); ++f)
      out.at(c, f) = static_cast<Sample>(static_cast<double>(buffer.at(c, f)) * gain);
  return out;
}

PreparedClip materialize(const AudioBuffer& full, double start_s, double end_s, PrepareConfig cfg) {
  SFERIC_SCOPE("prepare::materialize");

  PreparedClip out;
  out.start_s = start_s;
  out.end_s = end_s;
  AudioBuffer segment = cut(full, start_s, end_s);
  if (cfg.extract_stereo) out.stereo = analysis::extract_stereo_profile(segment, cfg.max_itd_s);
  AudioBuffer mono = to_mono(segment, cfg.mono);
  out.mono = normalize_peak(mono, cfg.target_peak, &out.gain_applied);

  SFERIC_LOG(Info, "materialize " + std::to_string(start_s) + "–" + std::to_string(end_s) +
                       "s gain=" + std::to_string(out.gain_applied) + " peak->" +
                       std::to_string(cfg.target_peak));
  return out;
}

}  // namespace sferic::analysis::prepare
