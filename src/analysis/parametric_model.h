#pragma once

#include <cstddef>
#include <vector>

#include "analysis/distribution_fitter.h"
#include "analysis/spectral_envelope.h"

namespace sferic {
namespace analysis {

// ADSR-style description of a signal's amplitude evolution over time.
// Attack = 10%→90% of peak RMS; release = 90%→10%.

// TODO analyze windows shape
// SOLUTION: for every envelope re-analyze and apply correct attack/release 'power'
struct TemporalEnvelope {
  double attack_time_s;   // 10% → 90% of peak RMS
  double peak_time_s;     // offset from sound start to global RMS peak
  double sustain_level;   // mean RMS in [attack_end, release_start], normalised 0–1
  double release_time_s;  // 90% → 10% of peak RMS
  double noise_floor_db;  // pre-attack or post-release RMS floor, dB re peak

  std::vector<double> rms_trajectory;  // RMS per analysis frame (same count as SpectralEnvelope)

  // Decomposed characterisation — components that together reproduce the trajectory.
  DistributionFit macro_shape;                  // PiecewiseLinear — smooth contour at data-driven knots,
                                                // peak-normalised; weights = interleaved [t₀,v₀, t₁,v₁, ...].
                                                // params[0] = smooth_peak (absolute scale to denormalise).
  DistributionFit residual_distribution;        // Lognormal/Gamma — per-frame rms / macro value (mult. noise)
  double residual_autocorr = 0.0;               // lag-1 autocorrelation of the residual series — how fast the
                                                // jitter wanders; drives AR(1) texture at synthesis (0 = white)
  DistributionFit spike_interval_distribution;  // Exponential/Weibull — inter-arrival times (s) of spikes
};

// Time-averaged spectral shape snapshot — one per EnvelopeFrame or one summary for the whole model.
struct SpectralShape {
  double centroid_hz;     // spectral center of gravity weighted by magnitude
  double spread_hz;       // standard deviation of frequency around centroid
  double tilt_db_octave;  // least-squares slope of log-magnitude vs log-frequency
  double flatness;        // geometric/arithmetic ratio — 0 = tonal, 1 = white noise
  double rolloff_hz;      // frequency below which 85% of total energy lies
};

// Temporal envelope for a single analysis band — derived from a contiguous
// frequency region identified by movement-dissimilarity analysis.
struct BandEnvelope {
  double center_hz;          // geometric mean of the region's frequency bounds
  double bandwidth_hz;       // width of the region in Hz
  size_t bin_lo;             // first FFT bin in this band (inclusive)
  size_t bin_hi;             // last FFT bin in this band (inclusive)
  double energy_fraction;    // mean fraction of total spectral energy in [bin_lo, bin_hi]
  SpectralShape mean_shape;  // spectral shape computed within this band's frequency range only

  // Per-frame trajectories — one entry per source SpectralEnvelope frame
  TemporalEnvelope envelope;              // amplitude (gain) over time — rms per frame
  std::vector<double> spread_trajectory;  // spread_hz per frame — how the band's spectral width varies
  std::vector<double> q_trajectory;       // center_hz / spread_hz per frame — Q factor over time
};

// A single tracked spectral peak and how it evolves over its lifetime.
// Frequency movement is decomposed into a linear drift plus residual jitter.
struct SpectralPeakTrack {
  double birth_time_s;                  // time when peak first exceeds prominence threshold
  double death_time_s;                  // time when peak drops below threshold
  double initial_frequency_hz;          // frequency at birth
  double initial_amplitude_db;          // amplitude at birth
  double q_factor;                      // centre frequency / -3 dB bandwidth
  double frequency_drift_hz_per_s;      // linear glide rate; negative = downward (typical thunder)
  DistributionFit frequency_jitter;     // residual frequency variation after drift subtracted
  DistributionFit amplitude_variation;  // frame-to-frame amplitude change distribution
};

// Human-readable, reproducible description of an analysed sound.
// Every field is a named, typed, physically-grounded quantity — no raw envelope arrays.
// Produced by ParametricExtractor; feeds directly into physics-path synthesis or code generation.
struct ParametricModel {
  double sample_rate;         // Hz of the analysed source
  double duration_s;          // clip length, seconds
  size_t source_frame_count;  // number of frames in the SpectralEnvelope this was derived from
  size_t num_bins;            // FFT bins per frame — sets frequency resolution of synthesis

  TemporalEnvelope overall_envelope;               // whole-clip amplitude envelope
  std::vector<BandEnvelope> bands;                 // count determined by variance analysis
  SpectralShape mean_spectral_shape;               // time-averaged over all frames
  std::vector<SpectralShape> spectral_trajectory;  // one entry per source frame
  std::vector<SpectralPeakTrack> peak_tracks;      // tracked peaks across frames

  DistributionFit centroid_distribution;   // global centroid_hz samples
  DistributionFit spread_distribution;     // global spread_hz samples
  DistributionFit amplitude_distribution;  // global RMS / amplitude samples

  bool empty() const { return source_frame_count == 0; }
};

// Analyzes a SpectralEnvelope and produces a compact ParametricModel.
// All extraction methods are stateless — no configuration needed.
class ParametricExtractor {
 public:
  ParametricExtractor() = default;

  ParametricModel extract(const SpectralEnvelope& model) const;

 private:
  TemporalEnvelope compute_temporal_envelope(const SpectralEnvelope& model) const;
  std::vector<BandEnvelope> compute_band_envelopes(const SpectralEnvelope& model) const;

  // Shared ADSR analysis — called by both compute_temporal_envelope and compute_band_envelopes.
  // rms and times must have the same length and be in chronological order.
  TemporalEnvelope compute_temporal_envelope_from_rms(const std::vector<double>& rms,
                                                      const std::vector<double>& times) const;

  SpectralShape compute_spectral_shape(const EnvelopeFrame& frame) const;
  // Compute spectral shape restricted to bins [bin_lo, bin_hi] of the given frame.
  SpectralShape compute_spectral_shape_band(const EnvelopeFrame& frame, size_t bin_lo,
                                            size_t bin_hi) const;
  std::vector<SpectralPeakTrack> track_spectral_peaks(const SpectralEnvelope& model) const;
};

}  // namespace analysis
}  // namespace sferic
