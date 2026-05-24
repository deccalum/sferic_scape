#include "analysis/multi_resolution.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sferic {
namespace analysis {

MultiResConfig MultiResConfig::default_config(double sample_rate) {
  double nyquist = sample_rate / 2.0;
  MultiResConfig config;
  config.window_type = WindowType::HANN;

  // Band 1: Low frequencies (0 - 500 Hz) — long window for fine freq resolution
  // 8192 @ 44100 Hz => ~5.4 Hz resolution, ~46ms hop
  MultiResConfig::Pass low;
  low.stft.fft_size = 8192;
  low.stft.hop_size = 2048;
  low.stft.window_type = WindowType::HANN;
  low.freq_lo = 0.0;
  low.freq_hi = 500.0;
  config.passes.push_back(low);

  // Band 2: Mid frequencies (500 - 4000 Hz) — medium window
  // 4096 @ 44100 Hz => ~10.8 Hz resolution, ~23ms hop
  MultiResConfig::Pass mid;
  mid.stft.fft_size = 4096;
  mid.stft.hop_size = 1024;
  mid.stft.window_type = WindowType::HANN;
  mid.freq_lo = 500.0;
  mid.freq_hi = 4000.0;
  config.passes.push_back(mid);

  // Band 3: High frequencies (4000 Hz - Nyquist) — short window for time precision
  // 1024 @ 44100 Hz => ~43 Hz resolution, ~5.8ms hop
  MultiResConfig::Pass high;
  high.stft.fft_size = 1024;
  high.stft.hop_size = 256;
  high.stft.window_type = WindowType::HANN;
  high.freq_lo = 4000.0;
  high.freq_hi = nyquist;
  config.passes.push_back(high);

  return config;
}

MultiResolutionSTFT::MultiResolutionSTFT(const MultiResConfig& config, double sample_rate)
    : config_(config), sample_rate_(sample_rate) {
  if (config_.passes.empty())
    throw std::invalid_argument("MultiResConfig must have at least one pass");

  for (const auto& pass : config_.passes) {
    stfts_.emplace_back(pass.stft, sample_rate);

    ResolutionBand band;
    band.fft_size = pass.stft.fft_size;
    band.freq_lo = pass.freq_lo;
    band.freq_hi = pass.freq_hi;
    band.freq_resolution = sample_rate / static_cast<double>(pass.stft.fft_size);
    band.time_resolution = static_cast<double>(pass.stft.hop_size) / sample_rate;
    bands_.push_back(band);
  }
}

std::vector<MultiResFrame> MultiResolutionSTFT::analyze(const AudioBuffer& mono_buffer) const {
  if (mono_buffer.num_channels() != 1)
    throw std::invalid_argument("MultiResolutionSTFT::analyze requires mono input");

  // Run all STFT passes
  std::vector<std::vector<SpectralFrame>> all_frames;
  for (const auto& stft : stfts_) {
    all_frames.push_back(stft.analyze(mono_buffer));
  }

  // Use the finest time resolution (smallest hop) as the output time grid
  size_t finest_pass = 0;
  double finest_time_res = stfts_[0].time_resolution();
  for (size_t p = 1; p < stfts_.size(); ++p) {
    if (stfts_[p].time_resolution() < finest_time_res) {
      finest_time_res = stfts_[p].time_resolution();
      finest_pass = p;
    }
  }

  const auto& time_grid = all_frames[finest_pass];
  if (time_grid.empty()) return {};

  std::vector<MultiResFrame> result;
  result.reserve(time_grid.size());

  for (size_t t = 0; t < time_grid.size(); ++t) {
    double target_time = time_grid[t].time_seconds;

    MultiResFrame merged;
    merged.time_seconds = target_time;

    // For each pass/band, find the nearest frame in time and extract bins
    // in the frequency range this pass is authoritative for
    for (size_t p = 0; p < config_.passes.size(); ++p) {
      const auto& pass = config_.passes[p];
      const auto& frames = all_frames[p];
      if (frames.empty()) continue;

      // Find nearest frame to target_time
      size_t nearest = 0;
      double min_dist = std::abs(frames[0].time_seconds - target_time);
      for (size_t f = 1; f < frames.size(); ++f) {
        double dist = std::abs(frames[f].time_seconds - target_time);
        if (dist < min_dist) {
          min_dist = dist;
          nearest = f;
        }
      }

      const auto& frame = frames[nearest];
      double bin_width = sample_rate_ / static_cast<double>(pass.stft.fft_size);

      // Extract bins in this band's frequency range
      size_t bin_lo = static_cast<size_t>(std::ceil(pass.freq_lo / bin_width));
      size_t bin_hi =
          std::min(static_cast<size_t>(std::floor(pass.freq_hi / bin_width)), frame.num_bins() - 1);

      for (size_t k = bin_lo; k <= bin_hi; ++k) {
        merged.frequencies.push_back(static_cast<double>(k) * bin_width);
        merged.magnitudes.push_back(frame.magnitudes[k]);
        merged.phases.push_back(frame.phases[k]);
      }
    }

    result.push_back(std::move(merged));
  }

  return result;
}

}  // namespace analysis
}  // namespace sferic
