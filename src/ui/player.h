#pragma once

#include <cstddef>
#include <functional>
#include <memory>

#include "core/buffer.h"
#include "io/audio_sink.h"

namespace sferic::ui {

// Builds an output sink for audio of a given shape.
// Injected so RealtimeSink is chosen once at the entry point.
using AudioSinkFactory =
    std::function<std::unique_ptr<io::AudioSink>(size_t channels, double sample_rate)>;

// Holds one sink open across clips, rebuilt only when the channel count or
// sample rate changes — a replay does not re-enumerate the output device.
// play() paces to the wall clock and returns when the block has drained.
class Player {
 public:
  explicit Player(AudioSinkFactory make_sink);
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  void play(const AudioBuffer& seg);

 private:
  AudioSinkFactory make_sink_;
  std::unique_ptr<io::AudioSink> sink_;
  size_t channels_ = 0;
  double sample_rate_ = 0.0;
};

}  // namespace sferic::ui
