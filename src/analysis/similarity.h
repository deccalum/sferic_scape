#pragma once

#include "analysis/spectral_envelope.h"

namespace sferic {
namespace analysis {

// Perceptual similarity between two magnitude spectral envelopes, as a percentage in [0, 100].
// Cosine similarity of the flattened magnitude grids after resampling `b` onto `a`'s
// time × frequency grid — scale-invariant, so it measures spectral *shape* agreement over
// time rather than absolute level (synthesis amplitude is not yet calibrated). 100 = identical.
double envelope_similarity(const SpectralEnvelope& a, const SpectralEnvelope& b);

}  // namespace analysis
}  // namespace sferic
