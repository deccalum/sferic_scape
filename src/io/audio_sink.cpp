#include "io/audio_sink.h"

#include <sstream>
#include <utility>

#include "core/logger.h"
#include "io/audio_file.h"

namespace sferic {
namespace io {

FileSink::FileSink(std::filesystem::path path, size_t num_channels, double sample_rate)
    : path_(std::move(path)), num_channels_(num_channels), sample_rate_(sample_rate) {
  std::ostringstream ss;
  ss << "path=" << path_ << "  channels=" << num_channels_ << "  sample_rate=" << sample_rate_;
  SFERIC_LOG(Info, ss.str());
}

void FileSink::write_block(const AudioBuffer& block) {
  const size_t n = block.num_frames();
  const size_t ch = std::min(num_channels_, block.num_channels());
  interleaved_.reserve(interleaved_.size() + n * num_channels_);
  for (size_t f = 0; f < n; ++f)
    for (size_t c = 0; c < num_channels_; ++c)
      interleaved_.push_back(c < ch ? block.at(c, f) : 0.0f);
  frames_ += n;
}

void FileSink::finalize() {
  SFERIC_SCOPE("FileSink::finalize");

  AudioBuffer out(num_channels_, frames_, sample_rate_);
  for (size_t f = 0; f < frames_; ++f)
    for (size_t c = 0; c < num_channels_; ++c)
      out.at(c, f) = interleaved_[f * num_channels_ + c];

  io::save(out, path_.string());

  std::ostringstream ss;
  ss << "wrote " << path_ << "  frames=" << frames_
     << "  duration=" << out.duration() << "s";
  SFERIC_LOG(Info, ss.str());
}

}  // namespace io
}  // namespace sferic
