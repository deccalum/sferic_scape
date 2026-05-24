#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "core/buffer.h"
#include "core/constants.h"
#include "io/audio_file.h"

using namespace sferic;

class AudioFileTest : public ::testing::Test {
 protected:
  std::string tmp_path;

  void SetUp() override {
    tmp_path = (std::filesystem::temp_directory_path() / "waves_test_output.wav").string();
  }

  void TearDown() override {
    std::remove(tmp_path.c_str());
  }
};

#ifdef WAVES_HAS_SNDFILE

TEST_F(AudioFileTest, SaveAndLoadRoundTrip) {
  // Create a buffer with a 440 Hz sine
  const size_t frames = 4410;  // 0.1 seconds at 44100
  AudioBuffer original(1, frames, SAMPLE_RATE_44100);

  for (size_t i = 0; i < frames; ++i) {
    double t = static_cast<double>(i) / SAMPLE_RATE_44100;
    original.at(0, i) = static_cast<Sample>(0.8 * std::sin(TWO_PI * 440.0 * t));
  }

  // Save
  io::save(original, tmp_path);

  // Load
  auto loaded = io::load(tmp_path);

  EXPECT_EQ(loaded.num_channels(), original.num_channels());
  EXPECT_EQ(loaded.num_frames(), original.num_frames());
  EXPECT_DOUBLE_EQ(loaded.sample_rate(), original.sample_rate());

  // Samples should match closely (float precision through WAV)
  for (size_t i = 0; i < frames; ++i) {
    EXPECT_NEAR(loaded.at(0, i), original.at(0, i), 1e-5f) << "Mismatch at frame " << i;
  }
}

TEST_F(AudioFileTest, SaveAndLoadStereo) {
  const size_t frames = 1000;
  AudioBuffer original(2, frames, SAMPLE_RATE_48000);

  for (size_t i = 0; i < frames; ++i) {
    double t = static_cast<double>(i) / SAMPLE_RATE_48000;
    original.at(0, i) = static_cast<Sample>(std::sin(TWO_PI * 440.0 * t));
    original.at(1, i) = static_cast<Sample>(std::sin(TWO_PI * 880.0 * t));
  }

  io::save(original, tmp_path);
  auto loaded = io::load(tmp_path);

  EXPECT_EQ(loaded.num_channels(), 2u);
  EXPECT_EQ(loaded.num_frames(), frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_NEAR(loaded.at(0, i), original.at(0, i), 1e-5f);
    EXPECT_NEAR(loaded.at(1, i), original.at(1, i), 1e-5f);
  }
}

TEST_F(AudioFileTest, FileInfo) {
  const size_t frames = 2205;
  AudioBuffer buf(1, frames, SAMPLE_RATE_44100);
  io::save(buf, tmp_path);

  auto fi = io::info(tmp_path);
  EXPECT_EQ(fi.num_channels, 1u);
  EXPECT_EQ(fi.num_frames, frames);
  EXPECT_DOUBLE_EQ(fi.sample_rate, SAMPLE_RATE_44100);
  EXPECT_EQ(fi.format, "WAV");
  EXPECT_NEAR(fi.duration, 0.05, 0.001);
}

TEST_F(AudioFileTest, LoadNonexistent) {
  EXPECT_THROW(io::load("/tmp/waves_nonexistent_file.wav"), std::runtime_error);
}

#else

TEST(AudioFileNoSndfile, ThrowsWithoutLibrary) {
  EXPECT_THROW(io::load("test.wav"), std::runtime_error);
  EXPECT_THROW(io::info("test.wav"), std::runtime_error);
  EXPECT_THROW(io::save(AudioBuffer{}, "test.wav"), std::runtime_error);
}

#endif
