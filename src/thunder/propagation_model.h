#pragma once

#include <cstddef>
#include <vector>

#include "core/types.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

/** @brief Geometrical spreading regime for a thunder source.
 *
 *  Selects the exponent @f$ n @f$ in the spreading term @f$ r^{-n} @f$.
 *  Use Cylindrical for all standard listening distances (> ~500 m).
 */
enum class SpreadingMode {
  Cylindrical,  ///< n = 0.5 — correct for a distant lightning channel
  WeakShock,    ///< n = 0.75 — near-field / very close strike
  Spherical,    ///< n = 1.0 — point-source approximation (over-attenuates)
};

/** @brief Atmospheric and geometric parameters for one propagation path. */
struct PropagationConfig {
  /** Slant (hypocentral) distance from source to listener (m).
   *  Use LightningSegment::slant_distance_m per segment. */
  double distance_m   = 5000.0;

  double temperature_c = 20.0;    ///< Air temperature (°C). Affects Q_a.
  double humidity_pct  = 60.0;    ///< Relative humidity (%). Affects Q_a.
  double pressure_hpa  = 1013.25; ///< Atmospheric pressure (hPa).

  SpreadingMode spreading = SpreadingMode::Cylindrical;
};

/** @brief Pre-computed frequency-domain gain curve for one propagation path.
 *
 *  Combines geometrical spreading and frequency-dependent Q attenuation.
 *  Bin layout must match the FFT size used by the caller.
 */
struct PropagationGain {
  double spreading_gain;          ///< @f$ r^{-n} @f$ linear amplitude factor
  std::vector<double> freq_bins;  ///< Centre frequency of each bin (Hz)
  std::vector<double> gains;      ///< Linear amplitude per bin (spreading × Q × absorption)
};

/** @brief Applies atmospheric propagation effects to a thunder waveform.
 *
 *  @details Two effects are applied in series:
 *  1. Geometrical spreading: @f$ A \propto r^{-n} @f$
 *  2. Frequency-dependent Q attenuation (Hong et al. 2023, eq. 10):
 *  @f[ \tilde{A}(r,f) = \frac{\tilde{A}_0}{r^n}
 *      \exp\!\left(-\frac{\pi f r}{v_{\text{air}} Q(f)}\right),
 *      \quad Q(f) = Q_0 f^{q} @f]
 *
 *  Atmospheric molecular absorption (Bass et al. 1995) is available as a
 *  separate term but is negligible relative to scattering for f < 100 Hz.
 *
 *  Processing is performed in the frequency domain (FFT → multiply → IFFT).
 */
class PropagationFilter {
 public:
  PropagationFilter(const PropagationConfig& config, double sample_rate, size_t fft_size = 4096);

  /** @brief Apply spreading gain and Q attenuation to @p audio in-place. */
  void apply(SampleBuffer& audio) const;

  /** @brief Linear amplitude gain from geometrical spreading alone (@f$ r^{-n} @f$). */
  double spreading_gain() const;

  /** @brief Q factor at @p f_hz using the empirical Hong et al. (2023) fit.
   *
   *  @f[ Q(f) = Q_0 f^{q} @f]
   *  Coefficients depend on SpreadingMode (see THUNDER_Q0_* in thunder_constants.h).
   */
  double q_factor(double f_hz) const;

  /** @brief Atmospheric absorption coefficient at @p f_hz (Bass et al. 1995).
   *  @return Attenuation in dB km⁻¹. Negligible vs. scattering below 100 Hz.
   */
  double atmospheric_absorption_db_per_km(double f_hz) const;

  /** @brief Combined linear gain: spreading × Q attenuation × absorption. */
  double total_gain(double f_hz) const;

 private:
  PropagationConfig config_;
  double sample_rate_;
  size_t fft_size_;

  static constexpr double ref_distance_m_ = 1.0;  // gain = 1 at this reference distance

  double q0() const;    // Q₀ coefficient for the configured SpreadingMode
  double qexp() const;  // q exponent for the configured SpreadingMode
};

}  // namespace thunder
}  // namespace sferic
