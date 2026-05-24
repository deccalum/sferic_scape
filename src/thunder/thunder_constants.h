#pragma once

/** @file thunder_constants.h
 *  @brief Physics and analysis constants for thunder synthesis.
 *
 *  All values are grounded in measured data or published formulae.
 *  Do not adjust a constant without updating its cited source.
 */

namespace sferic {
namespace thunder {

/** @name Spectral peak frequency — source, near-field
 *  Measured with calibrated microphone and S-transform analysis.
 *  Abeygunawardana et al., "Frequency Analysis of Thunder Features".
 *  @note These are SOURCE values. PropagationFilter shifts them downward
 *        at distance. Do not use the Hong et al. (2023) propagated value
 *        of 34–36 Hz as a synthesis target.
 *  @{
 */
constexpr double THUNDER_PEAK_FREQ_MIN_HZ     = 42.0;   ///< Lower bound of measured range (Hz)
constexpr double THUNDER_PEAK_FREQ_MAX_HZ     = 137.0;  ///< Upper bound of measured range (Hz)
constexpr double THUNDER_PEAK_FREQ_TYPICAL_HZ = 65.0;   ///< Default at ~100 kA (Hz)
constexpr double THUNDER_FUNDAMENTAL_CEIL_HZ  = 230.0;  ///< All thunder features below this (Hz)
constexpr double THUNDER_PEAK_FREQ_BAND_LO_HZ = 42.0;   ///< Lower bound of 82nd-percentile band (Hz)
constexpr double THUNDER_PEAK_FREQ_BAND_HI_HZ = 100.0;  ///< Upper bound of 82nd-percentile band (Hz)
/** @} */

/** @name Few (1969) — cylindrical blast formula
 *  @f[ f_{\max} = k \cdot v_{\text{air}} \sqrt{\frac{P_0}{E_l}} @f]
 *  @{
 */
constexpr double FEW_K = 0.63;  ///< Sedov–Taylor cylindrical blast scaling constant

/** Acoustic energy per unit length at ~100 kA (Hong et al. 2023, eq. 11).
 *  Corresponds to a propagated peak of ~35 Hz at 5 km urban. */
constexpr double LIGHTNING_ENERGY_PER_LENGTH_J_M = 4.0e6;  ///< J m⁻¹
/** @} */

/** @name Wang et al. (2019) — current to overpressure
 *  @f[ P_{\max}\ [\text{Pa}] = 0.432 \cdot C_{\max}\ [\text{kA}] - 0.19 @f]
 *  Typical result: 100 kA → ~43 Pa at source.
 *  @{
 */
constexpr double WANG_PRESSURE_SLOPE_PA_PER_KA = 0.432;
constexpr double WANG_PRESSURE_OFFSET_PA       = -0.19;
/** @} */

/** @name Propagation quality factor — Hong et al. (2023), eq. 10
 *  Empirically fitted to Seoul thunder data.
 *  @f[ Q(f) = Q_0 \cdot f^{q} @f]
 *  @{
 */
constexpr double THUNDER_Q0_CYLINDRICAL   = 5.2;   ///< Q₀ for n = 0.5 (cylindrical)
constexpr double THUNDER_QEXP_CYLINDRICAL = 1.58;  ///< q  for n = 0.5

constexpr double THUNDER_Q0_WEAK_SHOCK    = 4.1;   ///< Q₀ for n = 0.75 (weak shock)
constexpr double THUNDER_QEXP_WEAK_SHOCK  = 1.69;

constexpr double THUNDER_Q0_SPHERICAL     = 2.9;   ///< Q₀ for n = 1.0 (spherical)
constexpr double THUNDER_QEXP_SPHERICAL   = 1.84;
/** @} */

/** @name Geometrical spreading exponents
 *  Ribner & Roy (1982); Langston (2004).
 *  @{
 */
constexpr double SPREADING_N_CYLINDRICAL = 0.5;   ///< Use for distant lightning channel
constexpr double SPREADING_N_WEAK_SHOCK  = 0.75;  ///< Near-field / close strike
constexpr double SPREADING_N_SPHERICAL   = 1.0;   ///< Point-source approximation
/** @} */

/** @name Crack and clap time scales
 *  Approximate values at ~3 km listening distance.
 *  @{
 */
constexpr double CRACK_POSITIVE_PULSE_MIN_MS = 5.0;    ///< t_p lower bound (ms)
constexpr double CRACK_POSITIVE_PULSE_MAX_MS = 20.0;   ///< t_p upper bound (ms)
constexpr double CLAP_DURATION_MIN_MS        = 50.0;   ///< ms
constexpr double CLAP_DURATION_MAX_MS        = 300.0;  ///< ms
constexpr double THUNDER_AUDIBLE_RANGE_KM    = 20.0;   ///< Typically inaudible beyond (km)
/** @} */

/** @name Pre-crack stepped-leader precursor
 *  The descending stepped leader emits faint acoustic crackles ~100–500 ms
 *  before the main return-stroke.  Values are empirical; no published
 *  measurements of the precursor spectral peak were found.
 *  @{
 */
constexpr double PRE_CRACK_OFFSET_DEFAULT_S  = 0.25;   ///< Default lead time (s)
constexpr double PRE_CRACK_FREQ_DEFAULT_HZ   = 600.0;  ///< Spectral peak of precursor (Hz)
constexpr float  PRE_CRACK_AMPLITUDE_DEFAULT = 0.03f;  ///< Fraction of main crack peak amplitude
/** @} */

/** @name Stochastic tail crackles — Few (1969) secondary arrivals
 *  Discrete claps from kinked channel segments arrive as a Poisson process
 *  whose rate decays exponentially after the initial crack.
 *  @{
 */
constexpr double CRACKLE_RATE_DEFAULT_HZ = 4.0;   ///< Initial events per second (empirical)
constexpr double CRACKLE_DECAY_TAU_S     = 4.0;   ///< Rate halving time ~2.8 s (empirical)
constexpr double CRACKLE_PEAK_MIN_HZ     = 60.0;  ///< Lowest micro-crack spectral peak (Hz)
constexpr double CRACKLE_PEAK_MAX_HZ     = 200.0; ///< Highest micro-crack spectral peak (Hz)
constexpr double CRACKLE_DUR_MIN_S       = 0.010; ///< Shortest micro-crack (s)
constexpr double CRACKLE_DUR_MAX_S       = 0.050; ///< Longest micro-crack (s)
/** @} */

/** @name LF harmonic saturation defaults
 *  Sub-bass tube-style saturation to compensate for the perceived energy
 *  deficit below ~120 Hz in synthesised thunder.  Empirical — no citation.
 *  @{
 */
constexpr double LF_SAT_CORNER_HZ_DEFAULT  = 120.0; ///< LPF corner before saturation (Hz)
constexpr double LF_SAT_DRIVE_DB_DEFAULT   = 8.0;   ///< Soft-clip drive (dB)
constexpr float  LF_SAT_MIX_DEFAULT        = 0.4f;  ///< Wet/dry blend (0–1)
/** @} */

/** @name CrackAnalyzer STFT defaults
 *  Short windows are required to resolve the crack in time.
 *  The default StochasticAnalyzer uses fft_size = 2048 (46 ms), which is
 *  too coarse; a 200 ms crack would yield only ~4 frames.
 *  @{
 */
constexpr size_t CRACK_FFT_SIZE_DEFAULT = 256;  ///< 5.8 ms at 44100 Hz
constexpr size_t CRACK_HOP_SIZE_DEFAULT = 64;   ///< 1.5 ms hop
constexpr size_t CRACK_FFT_SIZE_CLOSE   = 128;  ///< 2.9 ms — for very close-range recordings
constexpr size_t CRACK_ENVELOPE_POINTS  = 64;   ///< Spectral resolution of output StochasticModel
/** @} */

}  // namespace thunder
}  // namespace sferic
