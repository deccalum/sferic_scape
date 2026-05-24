#pragma once

#include <cstddef>
#include <optional>

#include "analysis/multi_resolution.h"
#include "analysis/stochastic_model.h"
#include "core/buffer.h"
#include "thunder/thunder_constants.h"

namespace sferic {
namespace thunder {

/** @brief Acoustic feature classification for a detected thunder onset.
 *
 *  Based on the three-feature taxonomy of Abeygunawardana et al.,
 *  "Frequency Analysis of Thunder Features", with spectral and duration
 *  thresholds derived from their measured data.
 */
enum class ThunderFeature {
  Unknown,

  /** Sharp transient modelled as a point source. Spectral peak 42–137 Hz
   *  (>82% of measured strikes in 42–100 Hz); attack < 5 ms; duration < 50 ms. */
  Peak,

  /** Shock wave front followed by pressure fluctuations. Lower spectral
   *  centroid than Peak; duration 50–300 ms. Dominant audible feature at
   *  most listening distances. */
  Clap,

  /** Extended low-frequency noise from staggered arrivals across the full
   *  bolt length. Duration > 300 ms. Not reverberation. */
  Rumble,
};

/** @brief Return a human-readable name for a ThunderFeature value. */
const char* thunder_feature_name(ThunderFeature f);

/** @brief Configuration for CrackAnalyzer.
 *
 *  Default values target a crack/clap recorded at typical outdoor distances
 *  (~1–5 km) at 44100 Hz sample rate.
 */
struct CrackAnalyzerConfig {
  /** STFT window size for stochastic analysis.
   *  Must be much shorter than the standard StochasticAnalyzer default of 2048
   *  (46 ms); a 200 ms crack would yield only ~4 frames at that size. */
  size_t fft_size = CRACK_FFT_SIZE_DEFAULT;  ///< 5.8 ms at 44100 Hz
  size_t hop_size = CRACK_HOP_SIZE_DEFAULT;  ///< 1.5 ms hop

  /** @brief Use MultiResolutionSTFT instead of a fixed-window STFT.
   *
   *  @details Abeygunawardana et al. use an S-transform, which provides
   *  frequency-dependent time resolution: long windows at low frequencies
   *  (better frequency resolution in the 42–100 Hz dominant band), short
   *  windows at high frequencies (better temporal resolution).
   *
   *  When true, three STFT passes are used:
   *  - LF (< 100 Hz):  fft_size × 8
   *  - MF (100–500 Hz): fft_size × 2
   *  - HF (> 500 Hz):  fft_size
   *
   *  This approximates the S-transform using the existing MultiResolutionSTFT
   *  infrastructure without a full S-transform implementation.
   */
  bool use_multiresolution = true;

  size_t num_envelope_points = CRACK_ENVELOPE_POINTS; ///< Spectral resolution of output StochasticModel

  /** Energy threshold for onset detection (dB above pre-onset RMS).
   *  Signal must exceed background RMS by this margin to be flagged as onset. */
  double onset_threshold_db = 20.0;

  double pre_onset_ms   = 5.0;    ///< Pre-roll before detected onset (ms). Preserves shock front rise.
  double crack_window_ms = 400.0; ///< Maximum window after onset (ms). Stops early if energy falls below decay_floor_db.
  double decay_floor_db  = -20.0; ///< Energy level (dB below peak) at which the crack is considered over.
  bool   normalise       = true;  ///< Normalise output StochasticModel to peak amplitude = 1.0.
};

/** @brief Output of a single CrackAnalyzer::analyze() call. */
struct CrackAnalysisResult {
  bool onset_found = false;

  ThunderFeature feature     = ThunderFeature::Unknown;
  size_t onset_sample        = 0;      ///< Sample index of detected onset in input buffer.
  double crack_duration_ms   = 0.0;
  double attack_ms           = 0.0;    ///< 10%–90% rise time (ms).
  double decay_ms            = 0.0;    ///< 90%–10% fall time (ms).

  /** Spectral peak of the crack averaged over early frames (Hz).
   *  @note This is the SOURCE peak (near-field). Expected range: 42–137 Hz.
   *        Values below ~35 Hz suggest a heavily propagated recording or
   *        that the window captured rumble rather than the crack. */
  double spectral_peak_hz = 0.0;

  analysis::StochasticModel crack_model; ///< Main output — drop into ThunderConfig::crack_profile.
  SampleBuffer source_window;            ///< Raw analyzed window, for comparison against synthesis output.
};

/** @brief Extracts a StochasticModel from the crack/clap portion of a thunder recording.
 *
 *  @details Pipeline:
 *  1. Onset detection — locate first energy spike above threshold
 *  2. Window selection — isolate onset ± pre_onset_ms to crack end
 *  3. Stochastic analysis — fixed or multi-resolution STFT (see CrackAnalyzerConfig)
 *  4. Feature classification — Peak / Clap / Rumble via spectral peak and duration
 *
 *  The output StochasticModel is normalised to peak amplitude = 1.0.
 *  ThunderSynth rescales it with peak_overpressure_pa() and propagation gain.
 */
class CrackAnalyzer {
 public:
  explicit CrackAnalyzer(const CrackAnalyzerConfig& config = {});

  /** @brief Analyze a mono audio buffer.
   *  @return Result with onset_found = false if no clear crack is detected.
   */
  CrackAnalysisResult analyze(const SampleBuffer& mono_audio, double sample_rate) const;

  /** @brief Analyze a multi-channel buffer, mixed to mono internally. */
  CrackAnalysisResult analyze(const AudioBuffer& audio) const;

 private:
  CrackAnalyzerConfig config_;

  // Returns -1 if no onset is found.
  int64_t detect_onset(const SampleBuffer& audio, double sample_rate) const;

  size_t find_crack_end(const SampleBuffer& audio,
                        size_t onset_sample,
                        double sample_rate) const;

  analysis::StochasticModel run_stochastic_fixed(const SampleBuffer& window,
                                                 double sample_rate) const;

  // Three-pass MultiResolutionSTFT approximating S-transform frequency-dependent resolution.
  analysis::StochasticModel run_stochastic_multiresolution(const SampleBuffer& window,
                                                           double sample_rate) const;

  analysis::StochasticModel run_stochastic(const SampleBuffer& window,
                                           double sample_rate) const;

  double estimate_spectral_peak(const analysis::StochasticModel& model,
                                double sample_rate) const;

  // Thresholds: Peak < 50 ms; Clap 50–300 ms; Rumble > 300 ms or peak < 42 Hz.
  ThunderFeature classify_feature(double spectral_peak_hz, double duration_ms) const;

  void measure_envelope(const SampleBuffer& window,
                        double sample_rate,
                        double& attack_ms,
                        double& decay_ms) const;
};

/** @brief Print a human-readable summary of @p result to stdout. */
void print_crack_analysis(const CrackAnalysisResult& result);

/** @brief Check whether the measured spectral peak is consistent with physics.
 *
 *  Compares CrackAnalysisResult::spectral_peak_hz against the source prediction
 *  from peak_frequency_hz() for @p current_ka.
 *
 *  @note Compares against the SOURCE peak (~65 Hz at 100 kA). Recordings made
 *        at distance will show a lower propagated peak and may fail validation
 *        even if the thunder was real. Default tolerance reflects the natural
 *        42–137 Hz spread across different strikes.
 */
bool validate_against_physics(const CrackAnalysisResult& result,
                               double current_ka,
                               double tolerance_hz = 25.0);

}  // namespace thunder
}  // namespace sferic
