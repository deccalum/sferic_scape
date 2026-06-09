#include "analysis/similarity.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/logger.h"

namespace sferic {
namespace analysis {

double envelope_similarity(const SpectralEnvelope& a, const SpectralEnvelope& b) {
  SFERIC_SCOPE("envelope_similarity");

  const size_t na     = a.ms.size();
  const size_t nb     = b.ms.size();
  const size_t bins_a = a.num_bins();
  const size_t bins_b = b.num_bins();

  // Cosine similarity over the flattened magnitude grid. `b` is index-resampled onto grid
  // `a` so differing frame counts / bin counts (different FFT sizes) still compare.
  double dot = 0.0, mag_a = 0.0, mag_b = 0.0;
  for (size_t i = 0; i < na; ++i) {
    const double fa = (na > 1) ? static_cast<double>(i) / static_cast<double>(na - 1) : 0.0;
    const size_t j  = static_cast<size_t>(std::lround(fa * static_cast<double>(nb - 1)));
    const auto& ea  = a.ms[i].envelope;
    const auto& eb  = b.ms[j].envelope;

    for (size_t k = 0; k < bins_a; ++k) {
      const double fb = (bins_a > 1) ? static_cast<double>(k) / static_cast<double>(bins_a - 1) : 0.0;
      const size_t kb = static_cast<size_t>(std::lround(fb * static_cast<double>(bins_b - 1)));
      const double va = ea[k];
      const double vb = eb[kb];
      dot   += va * vb;
      mag_a += va * va;
      mag_b += vb * vb;
    }
    SFERIC_PROGRESS(i, na, a.ms[i].time_seconds * 1000.0, 0);
  }

  const double denom = std::sqrt(mag_a) * std::sqrt(mag_b);
  const double cosine = (denom > 0.0) ? dot / denom : 0.0;
  const double pct = 100.0 * std::clamp(cosine, 0.0, 1.0);

  SFERIC_LOG(Info, "similarity " + std::to_string(pct) + "%");
  return pct;
}

}  // namespace analysis
}  // namespace sferic
