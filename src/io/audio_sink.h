#pragma once

#include <filesystem>

#include "core/buffer.h"
#include "core/types.h"

namespace sferic {
namespace io {

// Block-oriented consumer of synthesized audio. The realtime pipeline pushes
// fixed-size blocks; the sink decides what to do with them. A FileSink buffers
// and writes a WAV on finalize(); a future RealtimeSink would hand blocks to an
// audio backend (see README "Live Output").
class AudioSink {
 public:
  virtual ~AudioSink() = default;

  // Consume one block. Channel count / sample rate must match construction.
  virtual void write_block(const AudioBuffer& block) = 0;

  // Flush and close. After this call the sink must not receive more blocks.
  virtual void finalize() = 0;
};

// Accumulates blocks into a single AudioBuffer and writes it via io::save() on
// finalize(). Format is inferred from the path extension.
class FileSink : public AudioSink {
 public:
  FileSink(std::filesystem::path path, size_t num_channels, double sample_rate);

  void write_block(const AudioBuffer& block) override;
  void finalize() override;

 private:
  std::filesystem::path path_;
  size_t num_channels_;
  double sample_rate_;
  SampleBuffer interleaved_;  // [ch0_f0, ch1_f0, ch0_f1, ...] — grown per write_block
  size_t frames_ = 0;
};

}  // namespace io
}  // namespace sferic
