#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#include "analysis/parametric_model.h"
#include "analysis/spectral_envelope.h"
#include "core/buffer.h"
#include "io/audio_file.h"
#include "io/parametric_json.h"
#include "synthesis/noise_generator.h"
#include "synthesis/parametric_synth.h"

using namespace sferic;
using namespace sferic::analysis;
using namespace sferic::synthesis;

static constexpr const char* kWavPath = "SFERIC_INPUT";

TEST(Pipeline, StereoToMono) {
  // L=1, R=0 — mono must average to 0.5
  AudioBuffer stereo(2, 4410, 44100.0);
  for (size_t f = 0; f < 4410; ++f) {
    stereo.at(0, f) = 1.0f;
    stereo.at(1, f) = 0.0f;
  }

  AudioBuffer mono = stereo.to_mono();
  ASSERT_EQ(mono.num_channels(), 1u);
  for (size_t f = 0; f < 4410; ++f) EXPECT_NEAR(mono.at(0, f), 0.5f, 1e-5f);

  // analyze() must accept stereo without error
  SpectralAnalyzer analyzer(512, 128, 15);
  auto model = analyzer.analyze(stereo);
  EXPECT_FALSE(model.empty());
  EXPECT_EQ(model.sample_rate, 44100.0);
}

TEST(Pipeline, SampleRateFromBuffer) {
  AudioBuffer buf(1, 9600, 48000.0);
  SpectralAnalyzer analyzer(512, 0, 15);
  auto model = analyzer.analyze(buf);
  EXPECT_EQ(model.sample_rate, 48000.0);
}

TEST(Pipeline, RealRecordingSpectrals) {
  AudioBuffer audio = io::load(kWavPath);
  ASSERT_FALSE(audio.empty());
  std::printf("  [file]  channels=%zu  frames=%zu  rate=%.0fHz  duration=%.2fs\n", audio.num_channels(), audio.num_frames(), audio.sample_rate(), audio.duration());
  SpectralAnalyzer analyzer(2048, 512, 15);
  auto model = analyzer.analyze(audio);
  ASSERT_FALSE(model.empty());
  ASSERT_GT(model.num_bins(), 0u);

  // Mean spectral envelope across all ms-frames
  const size_t n_bins = model.num_bins();
  std::vector<double> mean_env(n_bins, 0.0);
  for (const auto& frame : model.ms) {
    for (size_t b = 0; b < frame.envelope.size(); ++b) mean_env[b] += frame.envelope[b];
  }
  for (double& v : mean_env) v /= static_cast<double>(model.ms.size());

  // Peak frequency (skipping DC bin at index 0)
  auto peak_it = std::max_element(mean_env.begin() + 1, mean_env.end());
  const double peak_hz = static_cast<double>(std::distance(mean_env.begin(), peak_it)) * model.ms[0].frequency_spacing;

  // Spectral centroid
  double w_sum = 0.0, f_sum = 0.0;
  for (size_t b = 0; b < n_bins; ++b) {
    const double f = static_cast<double>(b) * model.ms[0].frequency_spacing;
    w_sum += mean_env[b];
    f_sum += f * mean_env[b];
  }
  const double centroid_hz = (w_sum > 0.0) ? f_sum / w_sum : 0.0;
  std::printf("  [model] ms-frames=%zu  bins=%zu  spacing=%.2fHz\n", model.ms.size(), n_bins, model.ms[0].frequency_spacing);
  std::printf("  [spec]  peak=%.1fHz  centroid=%.1fHz\n", peak_hz, centroid_hz);
  EXPECT_GT(peak_hz, 0.0);
  EXPECT_GT(centroid_hz, 0.0);
}

using FramePeaks = std::vector<std::pair<size_t, double>>;

// Build a SpectralEnvelope from per-frame peak lists. Non-specified bins are zero.
// Frames are 1ms apart; freq_spacing controls Hz per bin.
static SpectralEnvelope make_model(const std::vector<FramePeaks>& frames,
                                   double freq_spacing = 10.0, size_t n_bins = 256,
                                   double sample_rate = 44100.0) {
  SpectralEnvelope m;
  m.sample_rate = sample_rate;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    EnvelopeFrame f;
    f.time_seconds = static_cast<double>(fi) * 0.001;
    f.frequency_spacing = freq_spacing;
    f.envelope.assign(n_bins, 0.0);
    for (auto [bin, mag] : frames[fi])
      if (bin < n_bins) f.envelope[bin] = mag;
    m.ms.push_back(std::move(f));
  }
  return m;
}

// Convenience — N frames all with the same single spike at `bin`, magnitude 1.
static SpectralEnvelope make_constant_spike(size_t n_frames, size_t bin, double freq_spacing = 10.0,
                                            size_t n_bins = 256, double sample_rate = 44100.0) {
  return make_model(std::vector<FramePeaks>(n_frames, {{bin, 1.0}}), freq_spacing, n_bins, sample_rate);
}

TEST(Pipeline, SpectralMirror) {
  AudioBuffer audio = io::load(kWavPath);
  ASSERT_FALSE(audio.empty());
  SpectralAnalyzer analyzer(2048, 512, 15);
  auto model = analyzer.analyze(audio);
  ASSERT_FALSE(model.empty());

  // Synthesize full duration via NoiseGenerator
  const size_t num_samples = audio.num_frames();
  NoiseGenerator gen;
  gen.load_model(model);
  std::vector<float> synth_buf(num_samples, 0.0f);
  gen.render(synth_buf.data(), num_samples, 0.0);
  AudioBuffer synth_audio(1, num_samples, model.sample_rate);
  for (size_t i = 0; i < num_samples; ++i) synth_audio.at(0, i) = synth_buf[i];

  // Re-analyze synthesized output
  auto synth_model = analyzer.analyze(synth_audio);
  ASSERT_FALSE(synth_model.empty());

  // Mean spectral envelope — original vs synthesized
  const size_t n_bins = model.num_bins();
  std::vector<double> orig_env(n_bins, 0.0), synth_env(n_bins, 0.0);
  for (const auto& frame : model.ms) {
    for (size_t b = 0; b < std::min(n_bins, frame.envelope.size()); ++b)
      orig_env[b] += frame.envelope[b];
  }
  for (double& v : orig_env) v /= static_cast<double>(model.ms.size());
  for (const auto& frame : synth_model.ms) {
    for (size_t b = 0; b < std::min(n_bins, frame.envelope.size()); ++b)
      synth_env[b] += frame.envelope[b];
  }
  for (double& v : synth_env) v /= static_cast<double>(synth_model.ms.size());

  // Cosine similarity between mean envelopes — scale-invariant
  double dot = 0.0, orig_norm = 0.0, synth_norm = 0.0;
  for (size_t b = 0; b < n_bins; ++b) {
    dot += orig_env[b] * synth_env[b];
    orig_norm += orig_env[b] * orig_env[b];
    synth_norm += synth_env[b] * synth_env[b];
  }
  const double accuracy = (orig_norm > 0.0 && synth_norm > 0.0) ? dot / (std::sqrt(orig_norm) * std::sqrt(synth_norm)) : 0.0;
  std::printf("  synthesis mirror accuracy: %.1f%%\n", accuracy * 100.0);
  EXPECT_GT(accuracy, 0.5);  // cosine similarity > 50%
}

TEST(TrackPeaks, SingleSteadyPeak) {
  // 50 frames, spike fixed at bin 50 = 500 Hz — one track, zero drift.
  auto model = make_constant_spike(50, 50);
  ParametricExtractor ext;
  auto pm = ext.extract(model);
  ASSERT_EQ(pm.peak_tracks.size(), 1u);
  const auto& t = pm.peak_tracks[0];
  EXPECT_NEAR(t.initial_frequency_hz, 500.0, 10.0);
  EXPECT_NEAR(t.birth_time_s, 0.0, 0.002);
  EXPECT_GT(t.death_time_s, 0.040);
  EXPECT_NEAR(t.frequency_drift_hz_per_s, 0.0, 50.0);
  EXPECT_GT(t.q_factor, 0.0);
}

TEST(TrackPeaks, TwoDistinctPeaks) {
  // 50 frames, simultaneous spikes at 300 Hz and 800 Hz — two independent tracks.
  std::vector<FramePeaks> frames(50, {{30u, 1.0}, {80u, 1.0}});
  auto model = make_model(frames);
  ParametricExtractor ext;
  auto pm = ext.extract(model);

  ASSERT_EQ(pm.peak_tracks.size(), 2u);

  auto tracks = pm.peak_tracks;
  std::sort(tracks.begin(), tracks.end(), [](const SpectralPeakTrack& a, const SpectralPeakTrack& b) {
              return a.initial_frequency_hz < b.initial_frequency_hz;
            });
  EXPECT_NEAR(tracks[0].initial_frequency_hz, 300.0, 10.0);
  EXPECT_NEAR(tracks[1].initial_frequency_hz, 800.0, 10.0);
}

TEST(TrackPeaks, LinearFrequencyDrift) {
  // 100 frames. Spike starts at bin 60 (600 Hz), drops 1 bin every 10 frames.
  // OLS drift = −90 Hz / 99ms ≈ −909 Hz/s; staircase approximation gives ≈ −990 Hz/s.
  std::vector<FramePeaks> frames(100);
  for (size_t fi = 0; fi < 100; ++fi) frames[fi] = {{60u - fi / 10u, 1.0}};
  auto model = make_model(frames);
  ParametricExtractor ext;
  auto pm = ext.extract(model);
  ASSERT_EQ(pm.peak_tracks.size(), 1u);
  EXPECT_LT(pm.peak_tracks[0].frequency_drift_hz_per_s, -700.0);   // strongly negative
  EXPECT_GT(pm.peak_tracks[0].frequency_drift_hz_per_s, -1300.0);  // not absurd
}

TEST(TrackPeaks, ShortPeakDiscarded) {
  // Spike in frames 20–24 only (5 frames < kMinTrackFrames=10) — must be dropped.
  std::vector<FramePeaks> frames(50);
  for (size_t fi = 20; fi < 25; ++fi) frames[fi] = {{50u, 1.0}};
  auto model = make_model(frames);
  ParametricExtractor ext;
  auto pm = ext.extract(model);
  EXPECT_EQ(pm.peak_tracks.size(), 0u);
}

// !USELESS WITHOUT REFERENCES
// TODO TEST(FitDistribution, TooFewSamplesReturnsEmpty) {
//   // 4 frames → centroid series has 4 values, below kMinSamples=5.
//   auto model = make_constant_spike(4, 50);
//   ParametricExtractor ext;
//   auto pm = ext.extract(model);

//   EXPECT_EQ(pm.centroid_distribution.sample_count, 0u);
// }

// TODO TEST(FitDistribution, SufficientSamplesProducesFit) {
//   // 20 frames with varying centroid — enough for all candidates to run.
//   // Alternate bins 48 and 52 → centroid series ≈ {480, 520, 480, ...}.
//   std::vector<FramePeaks> frames(20);
//   for (size_t fi = 0; fi < 20; ++fi)
//     frames[fi] = {{(fi % 2 == 0) ? 48u : 52u, 1.0}};

//   auto model = make_model(frames);
//   ParametricExtractor ext;
//   auto pm = ext.extract(model);

//   ASSERT_GT(pm.centroid_distribution.sample_count, 0u);
//   EXPECT_GE(pm.centroid_distribution.ks_p_value, 0.0);
//   // Winning distribution's location parameter — must be near the series centre.
//   // Normal stores mean in params[0]; Cauchy stores median (= 500 here).
//   EXPECT_NEAR(pm.centroid_distribution.params[0], 500.0, 25.0);
// }

// !USELESS WITHOUT REFERENCES
// TODO TEST(FitDistribution, NormalWinsForGaussianDeltas) {
//   // Build a track whose amplitude dB deltas are exactly the expected order
//   // statistics of N(0, σ=0.1 dB) for n=20. Normal must win over Cauchy since
//   // values are symmetric with no heavy tails.  Lognormal/Exponential don't
//   // compete because the deltas include negatives (all_positive = false).
//   static const double kDeltas[] = {
//     -0.1863, -0.1401, -0.1126, -0.0921, -0.0745, -0.0589, -0.0444,
//     -0.0313, -0.0187, -0.0062, +0.0062, +0.0187, +0.0313, +0.0444,
//     +0.0589, +0.0745, +0.0921, +0.1126, +0.1401, +0.1863
//   };  // N(0, 0.1) order statistics for n=20; symmetric, zero-mean

//   // Convert cumulative dB levels to linear magnitudes for 21 frames.
//   std::vector<FramePeaks> frames(21);
//   double db = 0.0;
//   for (size_t fi = 0; fi < 21; ++fi) {
//     frames[fi] = {{50u, std::pow(10.0, db / 20.0)}};
//     if (fi < 20) db += kDeltas[fi];
//   }

//   auto model = make_model(frames);
//   ParametricExtractor ext;
//   auto pm = ext.extract(model);

//   ASSERT_EQ(pm.peak_tracks.size(), 1u);
//   const auto& dist = pm.peak_tracks[0].amplitude_variation;
//   ASSERT_GT(dist.sample_count, 0u);
//   EXPECT_EQ(dist.kind, DistributionKind::Normal);
//   EXPECT_NEAR(dist.params[0], 0.0, 0.05);   // mean ≈ 0 dB/frame
//   EXPECT_NEAR(dist.params[1], 0.10, 0.03);  // stddev ≈ 0.1 dB/frame
// }

TEST(Pipeline, ParametricSynthOutput) {
  AudioBuffer audio = io::load(kWavPath);
  ASSERT_FALSE(audio.empty());
  SpectralAnalyzer analyzer(2048, 0, 15);
  SpectralEnvelope envelope = analyzer.analyze(audio);
  ASSERT_FALSE(envelope.empty());
  ParametricExtractor extractor;
  ParametricModel pm = extractor.extract(envelope);
  ASSERT_FALSE(pm.empty());
  std::printf("  [param]  duration=%.2fs  bands=%zu  frames=%zu\n", pm.duration_s, pm.bands.size(), pm.source_frame_count);
  std::printf("  [shape]  centroid=%.1fHz  tilt=%.2f dB/oct  flatness=%.3f  rolloff=%.1fHz\n", pm.mean_spectral_shape.centroid_hz, pm.mean_spectral_shape.tilt_db_octave, pm.mean_spectral_shape.flatness, pm.mean_spectral_shape.rolloff_hz);
  std::printf("  [env]    attack=%.1fms  peak=%.1fms  release=%.1fms\n", pm.overall_envelope.attack_time_s * 1000.0, pm.overall_envelope.peak_time_s * 1000.0, pm.overall_envelope.release_time_s * 1000.0);
  ParametricSynth synth;
  synth.load_model(pm);
  ASSERT_TRUE(synth.has_model());
  AudioBuffer out = synth.synthesize();
  ASSERT_FALSE(out.empty());
  ASSERT_EQ(out.num_channels(), 1u);
  EXPECT_NEAR(out.duration(), pm.duration_s, 0.1);
  const float peak = out.peak_amplitude();
  std::printf("  [synth]  samples=%zu  duration=%.2fs  peak=%.4f\n", out.num_frames(), out.duration(), peak);
  EXPECT_GT(peak, 0.0f);
  io::save(out, "/tmp/sferic_parametric_out.wav");
  std::printf("  [out]    /tmp/sferic_parametric_out.wav\n");
}

TEST(Pipeline, ParametricJsonRoundTrip) {
  AudioBuffer audio = io::load(kWavPath);
  SpectralAnalyzer analyzer(2048, 0, 15);
  SpectralEnvelope envelope = analyzer.analyze(audio);
  ParametricExtractor extractor;
  ParametricModel original = extractor.extract(envelope);
  ASSERT_FALSE(original.empty());

  // Save to JSON
  const std::string json_path = "/tmp/sferic_parametric.json";
  io::save_parametric_json(original, json_path);
  std::printf("  [json]   %s\n", json_path.c_str());

  // Reload
  ParametricModel reloaded = io::load_parametric_json(json_path);
  ASSERT_FALSE(reloaded.empty());

  // Core scalars survive the round-trip exactly
  EXPECT_EQ(reloaded.sample_rate, original.sample_rate);
  EXPECT_EQ(reloaded.duration_s, original.duration_s);
  EXPECT_EQ(reloaded.source_frame_count, original.source_frame_count);

  // Envelope ADSR
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.attack_time_s, original.overall_envelope.attack_time_s);
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.peak_time_s, original.overall_envelope.peak_time_s);
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.release_time_s, original.overall_envelope.release_time_s);
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.noise_floor_db, original.overall_envelope.noise_floor_db);

  // rms_trajectory length and first/last values
  ASSERT_EQ(reloaded.overall_envelope.rms_trajectory.size(), original.overall_envelope.rms_trajectory.size());
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.rms_trajectory.front(), original.overall_envelope.rms_trajectory.front());
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.rms_trajectory.back(), original.overall_envelope.rms_trajectory.back());

  // Mean spectral shape
  EXPECT_DOUBLE_EQ(reloaded.mean_spectral_shape.centroid_hz, original.mean_spectral_shape.centroid_hz);
  EXPECT_DOUBLE_EQ(reloaded.mean_spectral_shape.tilt_db_octave, original.mean_spectral_shape.tilt_db_octave);
  EXPECT_DOUBLE_EQ(reloaded.mean_spectral_shape.rolloff_hz, original.mean_spectral_shape.rolloff_hz);

  // Spectral trajectory length
  EXPECT_EQ(reloaded.spectral_trajectory.size(), original.spectral_trajectory.size());

  // Bands
  ASSERT_EQ(reloaded.bands.size(), original.bands.size());
  for (size_t i = 0; i < original.bands.size(); ++i) {
    EXPECT_EQ(reloaded.bands[i].bin_lo, original.bands[i].bin_lo);
    EXPECT_EQ(reloaded.bands[i].bin_hi, original.bands[i].bin_hi);
    EXPECT_DOUBLE_EQ(reloaded.bands[i].energy_fraction, original.bands[i].energy_fraction);
    EXPECT_EQ(reloaded.bands[i].spread_trajectory.size(), original.bands[i].spread_trajectory.size());
    EXPECT_EQ(reloaded.bands[i].q_trajectory.size(), original.bands[i].q_trajectory.size());
  }

  // Peak tracks
  EXPECT_EQ(reloaded.peak_tracks.size(), original.peak_tracks.size());

  // macro_shape round-trip (PiecewiseLinear — knot count and first knot preserved)
  EXPECT_EQ(reloaded.overall_envelope.macro_shape.kind, original.overall_envelope.macro_shape.kind);
  ASSERT_EQ(reloaded.overall_envelope.macro_shape.weights.size(), original.overall_envelope.macro_shape.weights.size());
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.macro_shape.weights.front(), original.overall_envelope.macro_shape.weights.front());
  EXPECT_DOUBLE_EQ(reloaded.overall_envelope.macro_shape.weights.back(), original.overall_envelope.macro_shape.weights.back());

  // residual_distribution — must be a real fit (Lognormal or Gamma expected)
  EXPECT_GT(reloaded.overall_envelope.residual_distribution.sample_count, 0u);
  EXPECT_EQ(reloaded.overall_envelope.residual_distribution.kind, original.overall_envelope.residual_distribution.kind);

  // spike_interval_distribution
  EXPECT_GT(reloaded.overall_envelope.spike_interval_distribution.sample_count, 0u);
  EXPECT_EQ(reloaded.overall_envelope.spike_interval_distribution.kind, original.overall_envelope.spike_interval_distribution.kind);

  // Top-level distributions (kind and sample_count must survive)
  EXPECT_EQ(reloaded.centroid_distribution.kind, original.centroid_distribution.kind);
  EXPECT_EQ(reloaded.centroid_distribution.sample_count, original.centroid_distribution.sample_count);

  std::printf("  [ok]     round-trip: %zu frames, %zu bands, %zu tracks\n", reloaded.source_frame_count, reloaded.bands.size(), reloaded.peak_tracks.size());
}

static size_t file_size(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  return f.good() ? static_cast<size_t>(f.tellg()) : 0;
}

TEST(Pipeline, ParametricJsonCompact) {
  AudioBuffer audio = io::load(kWavPath);
  SpectralAnalyzer analyzer(2048, 0, 15);
  SpectralEnvelope envelope = analyzer.analyze(audio);
  ParametricExtractor extractor;
  ParametricModel original = extractor.extract(envelope);
  ASSERT_FALSE(original.empty());
  const std::string full_path = "/tmp/sferic_parametric_full.json";
  const std::string compact_path = "/tmp/sferic_parametric_compact.json";
  io::save_parametric_json(original, full_path, io::TrajectoryMode::Full);
  io::save_parametric_json(original, compact_path, io::TrajectoryMode::Compact);
  const size_t full_bytes = file_size(full_path);
  const size_t compact_bytes = file_size(compact_path);
  std::printf("  [size]   full=%zu B  compact=%zu B  ratio=%.1fx smaller\n", full_bytes, compact_bytes, static_cast<double>(full_bytes) / static_cast<double>(compact_bytes));

  // Compact must be dramatically smaller — the per-frame arrays are the bulk.
  // (Remaining size is peak_tracks: discrete events, not trajectories.)
  EXPECT_LT(compact_bytes, full_bytes / 5);

  // Reload compact — arrays gone, every parameter intact.
  ParametricModel compact = io::load_parametric_json(compact_path);
  ASSERT_FALSE(compact.empty());
  EXPECT_TRUE(compact.overall_envelope.rms_trajectory.empty());
  EXPECT_TRUE(compact.spectral_trajectory.empty());
  ASSERT_EQ(compact.bands.size(), original.bands.size());
  for (const auto& b : compact.bands) {
    EXPECT_TRUE(b.envelope.rms_trajectory.empty());
    EXPECT_TRUE(b.spread_trajectory.empty());
    EXPECT_TRUE(b.q_trajectory.empty());
  }

  // Parameters needed for synthesis survive exactly.
  EXPECT_DOUBLE_EQ(compact.overall_envelope.macro_shape.params[0],  original.overall_envelope.macro_shape.params[0]);  // scale
  EXPECT_DOUBLE_EQ(compact.overall_envelope.residual_autocorr, original.overall_envelope.residual_autocorr);
  for (size_t i = 0; i < original.bands.size(); ++i) {
    EXPECT_EQ(compact.bands[i].bin_lo, original.bands[i].bin_lo);
    EXPECT_EQ(compact.bands[i].bin_hi, original.bands[i].bin_hi);
    EXPECT_DOUBLE_EQ(compact.bands[i].envelope.macro_shape.params[0], original.bands[i].envelope.macro_shape.params[0]);
  }

  // The compact model must still synthesize audio — no recorded array required.
  ParametricSynth synth;
  synth.load_model(compact);
  ASSERT_TRUE(synth.has_model());
  AudioBuffer out = synth.synthesize();
  ASSERT_FALSE(out.empty());
  EXPECT_GT(out.peak_amplitude(), 0.0f);
  std::printf("  [synth]  compact model -> peak=%.4f over %.2fs\n", out.peak_amplitude(), out.duration());
}
