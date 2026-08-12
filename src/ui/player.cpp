#include "ui/player.h"

#include <utility>

namespace sferic::ui {

Player::Player(AudioSinkFactory make_sink) : make_sink_(std::move(make_sink)) {}

Player::~Player() {
  if (sink_) sink_->finalize();
}

void Player::play(const AudioBuffer& seg) {
  if (!sink_ || seg.num_channels() != channels_ || seg.sample_rate() != sample_rate_) {
    if (sink_) sink_->finalize();
    sink_ = make_sink_(seg.num_channels(), seg.sample_rate());
    channels_ = seg.num_channels();
    sample_rate_ = seg.sample_rate();
  }
  sink_->write_block(seg);
}

}  // namespace sferic::ui
