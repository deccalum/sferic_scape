#pragma once

#include <filesystem>

#include "core/buffer.h"
#include "core/types.h"

namespace sferic::io {

// Block-oriented consumer of synthesized audio. The realtime pipeline pushes
// fixed-size blocks; the sink decides what to do with them. A FileSink buffers
// and writes a WAV on finalize(); a future RealtimeSink would hand blocks to an
// audio backend (see README "Live Output").
class AudioSink {
 public:
  virtual ~AudioSink() = default;
  virtual void write_block(const AudioBuffer& block) = 0;
  virtual void finalize() = 0;
};

class FileSink : public AudioSink {
 public:
  FileSink(std::filesystem::path path, size_t num_channels, double sample_rate);
  void write_block(const AudioBuffer& block) override;
  void finalize() override;

 private:
  std::filesystem::path path_;
  size_t num_channels_;
  double sample_rate_;
  SampleBuffer interleaved_;
  size_t frames_ = 0;
};

class RealtimeSink : public AudioSink {
 public:
  RealtimeSink(size_t num_channels, double sample_rate);
  ~RealtimeSink() override;
  void write_block(const AudioBuffer& block) override;
  void finalize() override;

 private:
  size_t num_channels_;
  double sample_rate_;
  void* stream_ = nullptr;
  bool open_ = false;
  SampleBuffer interleaved_;
};

}  // namespace sferic::io
