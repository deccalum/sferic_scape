#include "analysis/analysis_processor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

#include "analysis/spectral_envelope.h"
#include "core/logger.h"

namespace sferic {
namespace analysis {

bool EditMask::any() const {
  return std::any_of(cells.begin(), cells.end(), [](uint8_t c) {
    return c != 0;
  });
}

size_t EditMask::count() const {
  size_t n = 0;
  for (uint8_t c : cells) n += c;
  return n;
}

AnalysisProcessor::AnalysisProcessor(LocateConfig config) : cfg_(config) {}

size_t AnalysisProcessor::align_offset(const AudioBuffer& processed, const AudioBuffer& source,
                                       double segment_start_s) const {
  SFERIC_SCOPE("AnalysisProcessor::align_offset");

  const AudioBuffer p_mono = (processed.num_channels() == 1) ? processed : processed.to_mono();
  const AudioBuffer s_mono = (source.num_channels() == 1) ? source : source.to_mono();
  const double sr = s_mono.sample_rate();

  const size_t win = static_cast<size_t>(cfg_.align_window_s * sr);
  const size_t search = static_cast<size_t>(cfg_.align_search_s * sr);
  const size_t seed = static_cast<size_t>(segment_start_s * sr);

  const Sample* probe = p_mono.channel(0);
  double probe_norm = 0.0;
  for (size_t i = 0; i < win; ++i) probe_norm += static_cast<double>(probe[i]) * probe[i];
  probe_norm = std::sqrt(probe_norm);

  const size_t lo = (seed > search) ? seed - search : 0;
  const size_t hi = std::min(seed + search, s_mono.num_frames() - win);
  const Sample* src = s_mono.channel(0);

  // Cross-correlation: coarse stride, then refine ±stride at full resolution.
  const auto ncc = [&](size_t off) {
    double dot = 0.0, energy = 0.0;
    for (size_t i = 0; i < win; ++i) {
      const double s = src[off + i];
      dot += s * probe[i];
      energy += s * s;
    }
    return dot / (std::sqrt(energy) * probe_norm + 1e-12);
  };

  constexpr size_t kStride = 64;  // empirical — coarse search step
  double best = -1.0;
  size_t best_off = lo;
  for (size_t off = lo; off < hi; off += kStride) {
    const double c = ncc(off);
    if (c > best) {
      best = c;
      best_off = off;
    }
  }
  const size_t rlo = (best_off > kStride) ? best_off - kStride : 0;
  for (size_t off = rlo; off < best_off + kStride && off < hi; ++off) {
    const double c = ncc(off);
    if (c > best) {
      best = c;
      best_off = off;
    }
  }

  std::ostringstream ss;
  ss << "seed=" << segment_start_s << "s  aligned=" << static_cast<double>(best_off) / sr << "s"
     << "  xcorr=" << best;
  SFERIC_LOG(Info, ss.str());
  return best_off;
}

static SpectralEnvelope analyze(const AudioBuffer& buf, const LocateConfig& cfg) {
  return SpectralAnalyzer(cfg.fft_size, 0, cfg.smoothing_bins).analyze(buf);
}

// Median of a copied column — small per-bin vectors, nth_element is plenty.
static double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  const size_t mid = v.size() / 2;
  std::nth_element(v.begin(), v.begin() + mid, v.end());
  return v[mid];
}

// Drop connected components smaller than min_cells (4-neighbour flood fill).
static void filter_blobs(std::vector<uint8_t>& m, size_t nf, size_t nb, size_t min_cells) {
  const auto idx = [&](size_t f, size_t b) {
    return f * nb + b;
  };
  std::vector<uint8_t> visited(m.size(), 0);
  std::vector<size_t> stack, blob;
  for (size_t f0 = 0; f0 < nf; ++f0)
    for (size_t b0 = 0; b0 < nb; ++b0) {
      if (!m[idx(f0, b0)] || visited[idx(f0, b0)]) continue;
      stack.clear();
      blob.clear();
      stack.push_back(idx(f0, b0));
      visited[idx(f0, b0)] = 1;
      while (!stack.empty()) {
        const size_t cur = stack.back();
        stack.pop_back();
        blob.push_back(cur);
        const size_t f = cur / nb, b = cur % nb;
        const size_t nbrs[4][2] = {{f - 1, b}, {f + 1, b}, {f, b - 1}, {f, b + 1}};
        for (auto& nbr : nbrs) {
          const size_t nfr = nbr[0], nbn = nbr[1];
          if (nfr >= nf || nbn >= nb) continue;  // wraps to huge on underflow — caught here
          if (m[idx(nfr, nbn)] && !visited[idx(nfr, nbn)]) {
            visited[idx(nfr, nbn)] = 1;
            stack.push_back(idx(nfr, nbn));
          }
        }
      }
      if (blob.size() < min_cells)
        for (size_t c : blob) m[c] = 0;
    }
}

static void dump_debug(const std::string& path, size_t nf, size_t nb, double hop_s,
                       double freq_spacing, const std::vector<double>& sdb,
                       const std::vector<double>& pdb, const std::vector<uint8_t>& mask) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  const int64_t dims[2] = {static_cast<int64_t>(nf), static_cast<int64_t>(nb)};
  const double meta[2] = {hop_s, freq_spacing};
  std::fwrite(dims, sizeof(int64_t), 2, fp);
  std::fwrite(meta, sizeof(double), 2, fp);
  std::fwrite(sdb.data(), sizeof(double), sdb.size(), fp);
  std::fwrite(pdb.data(), sizeof(double), pdb.size(), fp);
  std::fwrite(mask.data(), sizeof(uint8_t), mask.size(), fp);
  std::fclose(fp);
  SFERIC_LOG(Info, "debug planes written: " + path);
}

void AnalysisProcessor::analyze_pair(const AudioBuffer& processed, const AudioBuffer& source,
                                     double segment_start_s, SpectralEnvelope& src_env,
                                     SpectralEnvelope& prc_env) const {
  const size_t offset = align_offset(processed, source, segment_start_s);

  // Slice the aligned source region matching the processed segment.
  const AudioBuffer s_mono = (source.num_channels() == 1) ? source : source.to_mono();
  const AudioBuffer p_mono = (processed.num_channels() == 1) ? processed : processed.to_mono();
  const size_t n = std::min(p_mono.num_frames(), s_mono.num_frames() - offset);
  AudioBuffer src_seg(1, n, s_mono.sample_rate());
  const Sample* src = s_mono.channel(0);
  std::copy(src + offset, src + offset + n, src_seg.channel(0));

  src_env = analyze(src_seg, cfg_);
  prc_env = analyze(p_mono, cfg_);
}

EditMask AnalysisProcessor::locate_edits(const AudioBuffer& processed, const AudioBuffer& source,
                                         double segment_start_s) const {
  SFERIC_SCOPE("AnalysisProcessor::locate_edits");
  SpectralEnvelope src_env, prc_env;
  analyze_pair(processed, source, segment_start_s, src_env, prc_env);
  return build_mask(src_env, prc_env);
}

PreprocessResult AnalysisProcessor::preprocess(const AudioBuffer& processed,
                                               const AudioBuffer& source,
                                               double segment_start_s) const {
  SFERIC_SCOPE("AnalysisProcessor::preprocess");
  PreprocessResult result;
  SpectralEnvelope src_env;
  analyze_pair(processed, source, segment_start_s, src_env, result.raw);
  result.mask = build_mask(src_env, result.raw);
  result.healed = heal(result.raw, result.mask);
  return result;
}

EditMask AnalysisProcessor::build_mask(const SpectralEnvelope& src_env,
                                       const SpectralEnvelope& prc_env) const {
  SFERIC_SCOPE("AnalysisProcessor::build_mask");
  const size_t nf = std::min(src_env.ms.size(), prc_env.ms.size());
  const size_t nb = src_env.num_bins();

  EditMask mask;
  mask.num_frames = nf;
  mask.num_bins = nb;
  mask.frequency_spacing = src_env.ms.front().frequency_spacing;
  mask.hop_seconds = (nf > 1) ? src_env.ms[1].time_seconds - src_env.ms[0].time_seconds : 0.0;
  mask.cells.assign(nf * nb, 0);

  // dB planes
  constexpr double kEps = 1e-9;
  std::vector<double> sdb(nf * nb), pdb(nf * nb);
  for (size_t f = 0; f < nf; ++f)
    for (size_t b = 0; b < nb; ++b) {
      sdb[f * nb + b] = 20.0 * std::log10(src_env.ms[f].envelope[b] + kEps);
      pdb[f * nb + b] = 20.0 * std::log10(prc_env.ms[f].envelope[b] + kEps);
    }

  // Per-bin median of (source − processed) over time absorbs any stationary EQ/level
  // difference; only time-localized removals survive.
  std::vector<double> bin_gain(nb);
  std::vector<double> col(nf);
  for (size_t b = 0; b < nb; ++b) {
    for (size_t f = 0; f < nf; ++f) col[f] = sdb[f * nb + b] - pdb[f * nb + b];
    bin_gain[b] = median(col);
  }

  // An erased cell shows both signatures — source clearly louder than processed (d), and
  // processed collapsed to near-silence. The collapse gate is floor- and gain-independent,
  // unlike a peak-relative threshold, so it catches faint edits the eraser zeroed out.
  for (size_t f = 0; f < nf; ++f) {
    for (size_t b = 0; b < nb; ++b) {
      const double d = (sdb[f * nb + b] - pdb[f * nb + b]) - bin_gain[b];
      const bool removed = d > cfg_.removal_thres_db;
      const bool collapsed = pdb[f * nb + b] < cfg_.silence_floor_db;
      mask.cells[f * nb + b] = (removed && collapsed) ? 1 : 0;
    }
    SFERIC_PROGRESS(f, nf, src_env.ms[f].time_seconds * 1000.0, 0);
  }

  filter_blobs(mask.cells, nf, nb, cfg_.min_blob_cells);

  {
    std::ostringstream ss;
    ss << "located " << mask.count() << " cells over " << nf << " frames × " << nb << " bins";
    SFERIC_LOG(Info, ss.str());
  }

  if (!cfg_.debug_dump_path.empty())
    dump_debug(cfg_.debug_dump_path, nf, nb, mask.hop_seconds, mask.frequency_spacing, sdb, pdb,
               mask.cells);

  return mask;
}

SpectralEnvelope AnalysisProcessor::heal(const SpectralEnvelope& env, const EditMask& mask) const {
  SFERIC_SCOPE("AnalysisProcessor::heal");
  SpectralEnvelope out = env;
  const size_t nf = std::min(env.ms.size(), mask.num_frames);
  const size_t nb = mask.num_bins;

  for (size_t b = 0; b < nb; ++b) {
    size_t f = 0;
    while (f < nf) {
      if (!mask.at(f, b)) {
        ++f;
        continue;
      }
      // Contiguous masked run [f0, f1] for this bin.
      const size_t f0 = f;
      while (f < nf && mask.at(f, b)) ++f;
      const size_t f1 = f - 1;

      const bool has_lo = f0 > 0;
      const bool has_hi = f1 + 1 < nf;
      const double v_lo = has_lo ? env.ms[f0 - 1].envelope[b] : 0.0;
      const double v_hi = has_hi ? env.ms[f1 + 1].envelope[b] : 0.0;

      if (has_lo && has_hi) {
        const double span = static_cast<double>(f1 - f0 + 2);
        for (size_t k = f0; k <= f1; ++k) {
          const double t = static_cast<double>(k - f0 + 1) / span;
          out.ms[k].envelope[b] = v_lo + t * (v_hi - v_lo);
        }
      } else {
        const double hold = has_lo ? v_lo : v_hi;  // flat extrapolation at segment edge
        for (size_t k = f0; k <= f1; ++k) out.ms[k].envelope[b] = hold;
      }
    }
    SFERIC_PROGRESS(b, nb, 0.0, 0);
  }

  SFERIC_LOG(Info, "healed " + std::to_string(mask.count()) + " cells across " +
                       std::to_string(nb) + " bins");
  return out;
}

}  // namespace analysis
}  // namespace sferic
