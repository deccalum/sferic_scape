#pragma once

#include <optional>
#include <random>
#include <vector>

#include "analysis/parametric_model.h"
#include "analysis/spectral_envelope.h"
#include "core/buffer.h"

namespace sferic {
namespace synthesis {

// Synthesizes audio from a ParametricModel — the inverse of ParametricExtractor.
// Reconstructs time-varying spectral envelopes from named scalars, then drives
// NoiseGenerator with the result. No original recording required.
class ParametricSynth {
 public:
  ParametricSynth();

  void load_model(const analysis::ParametricModel& model);
  void clear();

  bool has_model() const { return !model_.empty(); }

  // Synthesize audio. If duration_s is absent, uses model's original duration.
  AudioBuffer synthesize(std::optional<double> duration_s = std::nullopt) const;

  // Lazy, block-by-block synthesis for the realtime pipeline — no full
  // pre-reconstruction of the SpectralEnvelope. Call prepare_stream() once, then
  // render_block() repeatedly until it returns 0 (model exhausted).
  void prepare_stream(double duration_s);
  size_t render_block(float* out, size_t num_samples);

 private:
  analysis::ParametricModel model_;

  // Streaming state — set by prepare_stream(), advanced by render_block().
  size_t stream_total_frames_ = 0;
  size_t stream_pos_frames_ = 0;

  // Rebuild a SpectralEnvelope from parametric parameters for NoiseGenerator.
  analysis::SpectralEnvelope reconstruct_envelope(double duration_s) const;

  // Build an amplitude (RMS) series of length num_frames purely from a temporal
  // envelope's parameters: macro_shape contour × AR(1)-filtered residual noise.
  // This is the parametric replacement for reading env.rms_trajectory directly.
  std::vector<double> amplitude_series(const analysis::TemporalEnvelope& env, size_t num_frames,
                                       std::mt19937_64& rng) const;

  // Build one spectral envelope frame from a SpectralShape scaled by amplitude.
  // When bin_lo and bin_hi are provided, only bins [bin_lo, bin_hi] are populated;
  // all other bins are zero. The L2 normalization covers only the active bin range.
  std::vector<double> envelope_from_shape(const analysis::SpectralShape& shape, double amplitude,
                                          size_t num_bins, double freq_spacing, size_t bin_lo = 0,
                                          size_t bin_hi = SIZE_MAX) const;
};

}  // namespace synthesis
}  // namespace sferic
