#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "analysis/spectral_envelope.h"
#include "core/buffer.h"

namespace sferic {
namespace analysis {

// Configuration for locating spectral edits (regions a user erased from a recording).
struct LocateConfig {
  size_t fft_size = 2048;            // STFT resolution — matches the extraction grid
  size_t smoothing_bins = 15;        // frequency smoothing — matches SpectralAnalyzer default
  double align_window_s = 0.5;       // probe length for cross-correlation alignment
  double align_search_s = 3.0;       // ± search radius around the timestamp seed
  double removal_thres_db = 12.0;    // source louder than processed by this ⇒ removed
  double silence_floor_db = -100.0;  // processed must collapse below this — the erase signature
  size_t min_blob_cells = 40;        // drop connected regions smaller than this — alignment speckle
  std::string debug_dump_path;       // if set, write source/processed/mask planes for plotting
};

// A located set of removed time-frequency cells, on the same grid the ParametricExtractor sees.
struct EditMask {
  size_t num_frames = 0;
  size_t num_bins = 0;
  double frequency_spacing = 0.0;  // Hz per bin
  double hop_seconds = 0.0;        // seconds per frame
  std::vector<uint8_t> cells;      // [frame * num_bins + bin], 1 = removed

  uint8_t at(size_t frame, size_t bin) const { return cells[frame * num_bins + bin]; }
  bool any() const;
  size_t count() const;
};

// Output of a full pre-process pass: the located edits, the raw processed envelope, and the
// healed envelope (raw with erased cells filled). Extraction runs on `healed`.
struct PreprocessResult {
  EditMask mask;
  SpectralEnvelope raw;     // processed spectrogram, unmodified
  SpectralEnvelope healed;  // erased cells filled by per-bin interpolation over time
};

// Locates where a processed recording had spectral content erased, by aligning it against
// its untouched source and differencing the magnitude spectrograms, then heals those cells
// so the extractor never sees the silence. Pure pre-process before ParametricExtractor.
class AnalysisProcessor {
 public:
  explicit AnalysisProcessor(LocateConfig config = {});

  // `source` is the full untouched recording; `segment_start_s` is where `processed` was cut
  // from it (the segment timestamp), used to seed the fine alignment search.
  EditMask locate_edits(const AudioBuffer& processed, const AudioBuffer& source,
                        double segment_start_s) const;

  // Full pass — locate edits and heal them. Extract from result.healed.
  PreprocessResult preprocess(const AudioBuffer& processed, const AudioBuffer& source,
                              double segment_start_s) const;

  // Fill masked cells of `env` by linearly interpolating magnitude per bin across the gap,
  // holding the nearest good value at segment edges. Pure function — does not mutate `env`.
  SpectralEnvelope heal(const SpectralEnvelope& env, const EditMask& mask) const;

 private:
  LocateConfig cfg_;

  // Returns the source sample offset that best aligns `processed` against `source`,
  // searching ± align_search_s around the timestamp seed via normalised cross-correlation.
  size_t align_offset(const AudioBuffer& processed, const AudioBuffer& source,
                      double segment_start_s) const;

  // Align, slice the matching source region, and STFT both onto the same grid.
  void analyze_pair(const AudioBuffer& processed, const AudioBuffer& source, double segment_start_s,
                    SpectralEnvelope& src_env, SpectralEnvelope& prc_env) const;

  // Difference src/prc magnitude spectrograms into an EditMask (+ optional debug dump).
  EditMask build_mask(const SpectralEnvelope& src_env, const SpectralEnvelope& prc_env) const;
};

}  // namespace analysis
}  // namespace sferic
