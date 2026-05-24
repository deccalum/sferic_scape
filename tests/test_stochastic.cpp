#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "analysis/stochastic_model.h"
#include "core/buffer.h"

using namespace sferic;
using namespace sferic::analysis;

TEST(StochasticModel, AnalyzeSilence) {
  AudioBuffer silence(1, 8820, 44100.0);

  StochasticModelConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  cfg.num_envelope_points = 64;

  StochasticAnalyzer analyzer(cfg, 44100.0);
  auto model = analyzer.analyze(silence);

  EXPECT_FALSE(model.empty());
  EXPECT_EQ(model.num_envelope_points, 64u);

  for (const auto& frame : model.frames) {
    EXPECT_EQ(frame.envelope.size(), 64u);
    for (double v : frame.envelope) {
      EXPECT_NEAR(v, 0.0, 1e-6);
    }
  }
}

TEST(StochasticModel, AnalyzeNoise) {
  const size_t frames = 8820;
  AudioBuffer noise(1, frames, 44100.0);
  std::mt19937 rng(456);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  for (size_t i = 0; i < frames; ++i) {
    noise.at(0, i) = dist(rng);
  }

  StochasticModelConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  cfg.num_envelope_points = 64;
  cfg.smoothing_bins = 11;

  StochasticAnalyzer analyzer(cfg, 44100.0);
  auto model = analyzer.analyze(noise);

  EXPECT_FALSE(model.empty());

  bool has_nonzero = false;
  for (const auto& frame : model.frames) {
    for (double v : frame.envelope) {
      if (v > 1e-6) { has_nonzero = true; break; }
    }
    if (has_nonzero) break;
  }
  EXPECT_TRUE(has_nonzero);

  // Smoothed white noise should have a roughly flat envelope (within 20 dB)
  const auto& mid = model.frames[model.frames.size() / 2];
  double env_max = *std::max_element(mid.envelope.begin(), mid.envelope.end());
  double env_min = *std::min_element(mid.envelope.begin(), mid.envelope.end());
  if (env_min > 1e-8) {
    EXPECT_LT(env_max / env_min, 10.0);
  }
}

TEST(StochasticModel, RequiresMono) {
  AudioBuffer stereo(2, 4410, 44100.0);
  StochasticModelConfig cfg;
  cfg.fft_size = 1024;
  StochasticAnalyzer analyzer(cfg, 44100.0);
  EXPECT_THROW(analyzer.analyze(stereo), std::invalid_argument);
}

TEST(StochasticModel, ShortInputEmpty) {
  AudioBuffer short_buf(1, 100, 44100.0);
  StochasticModelConfig cfg;
  cfg.fft_size = 1024;
  StochasticAnalyzer analyzer(cfg, 44100.0);
  auto model = analyzer.analyze(short_buf);
  EXPECT_TRUE(model.empty());
}

TEST(StochasticModel, SynthesizeFromModel) {
  const size_t frames = 8820;
  AudioBuffer noise(1, frames, 44100.0);
  std::mt19937 rng(789);
  std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
  for (size_t i = 0; i < frames; ++i) {
    noise.at(0, i) = dist(rng);
  }

  StochasticModelConfig cfg;
  cfg.fft_size = 512;
  cfg.hop_size = 128;
  cfg.num_envelope_points = 32;

  StochasticAnalyzer analyzer(cfg, 44100.0);
  auto model = analyzer.analyze(noise);

  auto resynthesized = synthesize_stochastic(model, frames, 44100.0);
  EXPECT_EQ(resynthesized.num_frames(), frames);
  EXPECT_GT(resynthesized.rms(), 0.01);
}

TEST(StochasticModel, EmptyModelSilence) {
  StochasticModel empty_model;
  empty_model.sample_rate = 44100.0;
  empty_model.num_envelope_points = 64;

  auto buf = synthesize_stochastic(empty_model, 4410, 44100.0);
  EXPECT_NEAR(buf.peak_amplitude(), 0.0, 1e-10);
}
