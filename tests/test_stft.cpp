#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

#include "analysis/multi_resolution.h"
#include "analysis/stft.h"
#include "analysis/window.h"
#include "core/buffer.h"
#include "core/constants.h"

using namespace sferic;
using namespace sferic::analysis;

// --- Window tests ---

TEST(Window, HannSymmetry) {
  auto w = make_window(WindowType::HANN, 1024);
  EXPECT_EQ(w.size(), 1024u);

  // Hann window is symmetric
  for (size_t i = 0; i < w.size() / 2; ++i) {
    EXPECT_NEAR(w[i], w[w.size() - 1 - i], 1e-12);
  }

  // Endpoints should be 0 (or near 0)
  EXPECT_NEAR(w[0], 0.0, 1e-12);
  EXPECT_NEAR(w[w.size() - 1], 0.0, 1e-12);

  // Center should be 1.0
  EXPECT_NEAR(w[512], 1.0, 0.01);
}

TEST(Window, BlackmanHarrisRange) {
  auto w = make_window(WindowType::BLACKMAN_HARRIS, 4096);
  for (double v : w) {
    EXPECT_GE(v, -0.001);  // Allow tiny numerical noise
    EXPECT_LE(v, 1.001);
  }
}

TEST(Window, RectangularAllOnes) {
  auto w = make_window(WindowType::RECTANGULAR, 256);
  for (double v : w) {
    EXPECT_DOUBLE_EQ(v, 1.0);
  }
}

TEST(Window, CoherentGain) {
  auto rect = make_window(WindowType::RECTANGULAR, 100);
  EXPECT_NEAR(coherent_gain(rect), 1.0, 1e-12);

  auto hann = make_window(WindowType::HANN, 1024);
  // Hann coherent gain is ~0.5
  EXPECT_NEAR(coherent_gain(hann), 0.5, 0.01);
}

TEST(Window, KaiserGeneration) {
  auto w = make_window(WindowType::KAISER, 512, 8.0);
  EXPECT_EQ(w.size(), 512u);

  // Kaiser window: symmetric, positive
  for (size_t i = 0; i < w.size() / 2; ++i) {
    EXPECT_NEAR(w[i], w[w.size() - 1 - i], 1e-10);
  }
  for (double v : w) {
    EXPECT_GE(v, 0.0);
  }
}

// --- STFT tests ---

TEST(STFT, InvalidConfig) {
  // FFT size must be power of 2
  STFTConfig cfg;
  cfg.fft_size = 1000;
  EXPECT_THROW(STFT(cfg, 44100.0), std::invalid_argument);

  // Hop size must be <= FFT size
  cfg.fft_size = 1024;
  cfg.hop_size = 2048;
  EXPECT_THROW(STFT(cfg, 44100.0), std::invalid_argument);

  // Sample rate must be positive
  cfg.hop_size = 256;
  EXPECT_THROW(STFT(cfg, 0.0), std::invalid_argument);
}

TEST(STFT, EmptyInput) {
  STFTConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  STFT stft(cfg, 44100.0);

  AudioBuffer empty(1, 0, 44100.0);
  auto frames = stft.analyze(empty);
  EXPECT_TRUE(frames.empty());
}

TEST(STFT, ShortInput) {
  STFTConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  STFT stft(cfg, 44100.0);

  // Input shorter than FFT size
  AudioBuffer short_buf(1, 512, 44100.0);
  auto frames = stft.analyze(short_buf);
  EXPECT_TRUE(frames.empty());
}

TEST(STFT, SilenceProducesZeros) {
  STFTConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  STFT stft(cfg, 44100.0);

  AudioBuffer silence(1, 2048, 44100.0);
  auto frames = stft.analyze(silence);

  EXPECT_FALSE(frames.empty());
  for (const auto& frame : frames) {
    for (double mag : frame.magnitudes) {
      EXPECT_NEAR(mag, 0.0, 1e-10);
    }
  }
}

TEST(STFT, PureSinePeakDetection) {
  // A 1000 Hz sine should produce a peak near bin 1000/freq_resolution
  const double sr = 44100.0;
  const double freq = 1000.0;
  const size_t fft_size = 4096;
  const size_t duration_frames = fft_size * 4;

  AudioBuffer buf(1, duration_frames, sr);
  for (size_t i = 0; i < duration_frames; ++i) {
    double t = static_cast<double>(i) / sr;
    buf.at(0, i) = static_cast<Sample>(std::sin(TWO_PI * freq * t));
  }

  STFTConfig cfg;
  cfg.fft_size = fft_size;
  cfg.hop_size = fft_size / 4;
  cfg.window_type = WindowType::HANN;
  STFT stft(cfg, sr);

  auto frames = stft.analyze(buf);
  ASSERT_FALSE(frames.empty());

  // Check middle frame (avoid edge effects)
  const auto& frame = frames[frames.size() / 2];
  double bin_width = sr / static_cast<double>(fft_size);
  size_t expected_bin = static_cast<size_t>(std::round(freq / bin_width));

  // Find the bin with maximum magnitude
  size_t max_bin = 0;
  double max_mag = 0.0;
  for (size_t k = 0; k < frame.num_bins(); ++k) {
    if (frame.magnitudes[k] > max_mag) {
      max_mag = frame.magnitudes[k];
      max_bin = k;
    }
  }

  // Peak should be within 1 bin of expected
  EXPECT_NEAR(static_cast<double>(max_bin), static_cast<double>(expected_bin), 1.0)
      << "Peak at bin " << max_bin << " (" << max_bin * bin_width << " Hz)"
      << ", expected bin " << expected_bin << " (" << freq << " Hz)";

  // Peak should be significantly above noise floor
  double noise_floor = 0.0;
  size_t noise_count = 0;
  for (size_t k = 0; k < frame.num_bins(); ++k) {
    if (std::abs(static_cast<int>(k) - static_cast<int>(expected_bin)) > 20) {
      noise_floor += frame.magnitudes[k];
      noise_count++;
    }
  }
  noise_floor /= static_cast<double>(noise_count);
  EXPECT_GT(max_mag, noise_floor * 100.0);  // At least 40 dB above noise
}

TEST(STFT, TwoSinesProduceTwoPeaks) {
  const double sr = 44100.0;
  const double freq1 = 440.0;
  const double freq2 = 1000.0;
  const size_t fft_size = 4096;
  const size_t duration_frames = fft_size * 4;

  AudioBuffer buf(1, duration_frames, sr);
  for (size_t i = 0; i < duration_frames; ++i) {
    double t = static_cast<double>(i) / sr;
    buf.at(0, i) = static_cast<Sample>(0.5 * std::sin(TWO_PI * freq1 * t) +
                                       0.5 * std::sin(TWO_PI * freq2 * t));
  }

  STFTConfig cfg;
  cfg.fft_size = fft_size;
  cfg.hop_size = fft_size / 4;
  STFT stft(cfg, sr);

  auto frames = stft.analyze(buf);
  const auto& frame = frames[frames.size() / 2];

  double bin_width = sr / static_cast<double>(fft_size);
  size_t bin1 = static_cast<size_t>(std::round(freq1 / bin_width));
  size_t bin2 = static_cast<size_t>(std::round(freq2 / bin_width));

  // Both bins should have significant magnitude
  // Check local maximum near each expected frequency
  auto find_local_max = [&](size_t center_bin, size_t radius) {
    double max_mag = 0.0;
    for (size_t k = (center_bin > radius ? center_bin - radius : 0);
         k <= std::min(center_bin + radius, frame.num_bins() - 1); ++k) {
      max_mag = std::max(max_mag, frame.magnitudes[k]);
    }
    return max_mag;
  };

  double peak1 = find_local_max(bin1, 2);
  double peak2 = find_local_max(bin2, 2);

  EXPECT_GT(peak1, 0.0);
  EXPECT_GT(peak2, 0.0);
  // Both peaks should have similar magnitude (equal amplitude input)
  EXPECT_NEAR(peak1 / peak2, 1.0, 0.2);
}

TEST(STFT, NormalizedAmplitude) {
  // A sine at amplitude A should produce a peak magnitude of ~A
  // after normalization. Use rectangular window for clean result.
  const double sr = 44100.0;
  const size_t fft_size = 1024;
  const double amplitude = 0.75;
  // Use a frequency that lands exactly on a bin for clean results
  const double freq = sr / static_cast<double>(fft_size) * 100.0;  // bin 100

  AudioBuffer buf(1, fft_size, sr);
  for (size_t i = 0; i < fft_size; ++i) {
    double t = static_cast<double>(i) / sr;
    buf.at(0, i) = static_cast<Sample>(amplitude * std::sin(TWO_PI * freq * t));
  }

  STFTConfig cfg;
  cfg.fft_size = fft_size;
  cfg.hop_size = fft_size;
  cfg.window_type = WindowType::RECTANGULAR;
  STFT stft(cfg, sr);

  auto frames = stft.analyze(buf);
  ASSERT_EQ(frames.size(), 1u);

  // Find peak magnitude
  double max_mag = 0.0;
  for (size_t k = 0; k < frames[0].num_bins(); ++k) {
    max_mag = std::max(max_mag, frames[0].magnitudes[k]);
  }

  // Peak should be close to the input amplitude
  EXPECT_NEAR(max_mag, amplitude, amplitude * 0.05)
      << "Peak magnitude " << max_mag << " should be ~" << amplitude;
}

TEST(STFT, FrameTimestamps) {
  const double sr = 44100.0;
  STFTConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  STFT stft(cfg, sr);

  AudioBuffer buf(1, 4096, sr);
  auto frames = stft.analyze(buf);

  // Timestamps should be evenly spaced by hop_size/sr
  double expected_dt = 256.0 / sr;
  for (size_t i = 1; i < frames.size(); ++i) {
    double dt = frames[i].time_seconds - frames[i - 1].time_seconds;
    EXPECT_NEAR(dt, expected_dt, 1e-10);
  }
}

TEST(STFT, FrequencyResolution) {
  STFTConfig cfg;
  cfg.fft_size = 4096;
  cfg.hop_size = 1024;
  STFT stft(cfg, 44100.0);

  EXPECT_NEAR(stft.frequency_resolution(), 44100.0 / 4096.0, 1e-10);
  EXPECT_EQ(stft.num_bins(), 2049u);
}

TEST(STFT, RequiresMono) {
  STFTConfig cfg;
  cfg.fft_size = 1024;
  cfg.hop_size = 256;
  STFT stft(cfg, 44100.0);

  AudioBuffer stereo(2, 2048, 44100.0);
  EXPECT_THROW(stft.analyze(stereo), std::invalid_argument);
}

// --- Multi-resolution tests ---

TEST(MultiResolution, DefaultConfig) {
  auto config = MultiResConfig::default_config(44100.0);
  EXPECT_EQ(config.passes.size(), 3u);
  EXPECT_DOUBLE_EQ(config.passes[0].freq_lo, 0.0);
  EXPECT_DOUBLE_EQ(config.passes[0].freq_hi, 500.0);
  EXPECT_DOUBLE_EQ(config.passes[2].freq_hi, 22050.0);
}

TEST(MultiResolution, AnalyzeSine) {
  const double sr = 44100.0;
  const double freq = 1000.0;            // Falls in mid band
  const size_t duration_frames = 44100;  // 1 second

  AudioBuffer buf(1, duration_frames, sr);
  for (size_t i = 0; i < duration_frames; ++i) {
    double t = static_cast<double>(i) / sr;
    buf.at(0, i) = static_cast<Sample>(std::sin(TWO_PI * freq * t));
  }

  auto config = MultiResConfig::default_config(sr);
  MultiResolutionSTFT mrstft(config, sr);

  auto frames = mrstft.analyze(buf);
  ASSERT_FALSE(frames.empty());

  // Check a middle frame: should have a peak near 1000 Hz
  const auto& frame = frames[frames.size() / 2];
  EXPECT_FALSE(frame.frequencies.empty());

  double max_mag = 0.0;
  double peak_freq = 0.0;
  for (size_t i = 0; i < frame.frequencies.size(); ++i) {
    if (frame.magnitudes[i] > max_mag) {
      max_mag = frame.magnitudes[i];
      peak_freq = frame.frequencies[i];
    }
  }

  // Peak should be near 1000 Hz (within one bin of the mid-resolution pass)
  double mid_bin_width = sr / 4096.0;  // ~10.8 Hz
  EXPECT_NEAR(peak_freq, freq, mid_bin_width * 2.0);
}

TEST(MultiResolution, BandsInfo) {
  auto config = MultiResConfig::default_config(44100.0);
  MultiResolutionSTFT mrstft(config, 44100.0);

  const auto& bands = mrstft.bands();
  EXPECT_EQ(bands.size(), 3u);

  // Low band: finest frequency resolution
  EXPECT_NEAR(bands[0].freq_resolution, 44100.0 / 8192.0, 0.01);
  // High band: finest time resolution
  EXPECT_LT(bands[2].time_resolution, bands[0].time_resolution);
}
