#include <gtest/gtest.h>

#include <cmath>

#include "core/buffer.h"
#include "core/constants.h"

using namespace sferic;

TEST(AudioBuffer, DefaultConstruction) {
  AudioBuffer buf;
  EXPECT_EQ(buf.num_channels(), 0u);
  EXPECT_EQ(buf.num_frames(), 0u);
  EXPECT_DOUBLE_EQ(buf.sample_rate(), 0.0);
  EXPECT_TRUE(buf.empty());
  EXPECT_DOUBLE_EQ(buf.duration(), 0.0);
}

TEST(AudioBuffer, Construction) {
  AudioBuffer buf(2, 44100, SAMPLE_RATE_44100);
  EXPECT_EQ(buf.num_channels(), 2u);
  EXPECT_EQ(buf.num_frames(), 44100u);
  EXPECT_DOUBLE_EQ(buf.sample_rate(), 44100.0);
  EXPECT_FALSE(buf.empty());
  EXPECT_DOUBLE_EQ(buf.duration(), 1.0);
}

TEST(AudioBuffer, SampleAccess) {
  AudioBuffer buf(2, 10, SAMPLE_RATE_44100);

  buf.at(0, 0) = 0.5f;
  buf.at(1, 0) = -0.5f;
  buf.at(0, 9) = 1.0f;

  EXPECT_FLOAT_EQ(buf.at(0, 0), 0.5f);
  EXPECT_FLOAT_EQ(buf.at(1, 0), -0.5f);
  EXPECT_FLOAT_EQ(buf.at(0, 9), 1.0f);
  EXPECT_FLOAT_EQ(buf.at(1, 9), 0.0f);
}

TEST(AudioBuffer, OutOfRange) {
  AudioBuffer buf(2, 10, SAMPLE_RATE_44100);
  EXPECT_THROW(buf.at(2, 0), std::out_of_range);
  EXPECT_THROW(buf.at(0, 10), std::out_of_range);
}

TEST(AudioBuffer, PeakAmplitude) {
  AudioBuffer buf(1, 4, SAMPLE_RATE_44100);
  buf.at(0, 0) = 0.3f;
  buf.at(0, 1) = -0.8f;
  buf.at(0, 2) = 0.5f;
  buf.at(0, 3) = 0.1f;

  EXPECT_FLOAT_EQ(buf.peak_amplitude(), 0.8f);
}

TEST(AudioBuffer, RMS) {
  AudioBuffer buf(1, 4, SAMPLE_RATE_44100);
  buf.at(0, 0) = 1.0f;
  buf.at(0, 1) = 1.0f;
  buf.at(0, 2) = 1.0f;
  buf.at(0, 3) = 1.0f;

  EXPECT_DOUBLE_EQ(buf.rms(), 1.0);
}

TEST(AudioBuffer, ToMono) {
  AudioBuffer stereo(2, 3, SAMPLE_RATE_44100);
  stereo.at(0, 0) = 0.5f;
  stereo.at(1, 0) = 1.0f;
  stereo.at(0, 1) = -0.2f;
  stereo.at(1, 1) = 0.4f;
  stereo.at(0, 2) = 0.0f;
  stereo.at(1, 2) = 0.6f;

  auto mono = stereo.to_mono();
  EXPECT_EQ(mono.num_channels(), 1u);
  EXPECT_EQ(mono.num_frames(), 3u);
  EXPECT_FLOAT_EQ(mono.at(0, 0), 0.75f);
  EXPECT_FLOAT_EQ(mono.at(0, 1), 0.1f);
  EXPECT_FLOAT_EQ(mono.at(0, 2), 0.3f);
}

TEST(AudioBuffer, Resize) {
  AudioBuffer buf;
  EXPECT_TRUE(buf.empty());

  buf.resize(1, 100, SAMPLE_RATE_48000);
  EXPECT_EQ(buf.num_channels(), 1u);
  EXPECT_EQ(buf.num_frames(), 100u);
  EXPECT_DOUBLE_EQ(buf.sample_rate(), 48000.0);
}

TEST(AudioBuffer, Clear) {
  AudioBuffer buf(2, 100, SAMPLE_RATE_44100);
  buf.clear();
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.num_channels(), 0u);
  EXPECT_EQ(buf.num_frames(), 0u);
}

TEST(AudioBuffer, MonoIdentity) {
  AudioBuffer mono(1, 5, SAMPLE_RATE_44100);
  mono.at(0, 0) = 0.1f;
  mono.at(0, 1) = 0.2f;

  auto result = mono.to_mono();
  EXPECT_EQ(result.num_channels(), 1u);
  EXPECT_FLOAT_EQ(result.at(0, 0), 0.1f);
  EXPECT_FLOAT_EQ(result.at(0, 1), 0.2f);
}

TEST(AudioBuffer, SineWaveRMS) {
  // RMS of a sine wave = amplitude / sqrt(2)
  const size_t frames = 44100;
  const double freq = 440.0;
  AudioBuffer buf(1, frames, SAMPLE_RATE_44100);

  for (size_t i = 0; i < frames; ++i) {
    double t = static_cast<double>(i) / SAMPLE_RATE_44100;
    buf.at(0, i) = static_cast<Sample>(std::sin(TWO_PI * freq * t));
  }

  double expected_rms = 1.0 / std::sqrt(2.0);
  EXPECT_NEAR(buf.rms(), expected_rms, 0.001);
}
