#include "io/audio_sink.h"

#include <fcntl.h>
#include <portaudio.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/logger.h"
#include "io/audio_file.h"

namespace sferic::io {
namespace {

void pa_check(PaError err, const char* what) {
  if (err != paNoError)
    throw std::runtime_error(std::string("PortAudio ") + what + ": " + Pa_GetErrorText(err));
}

struct SilenceStderr {
  int saved_ = -1;
  int devnull_ = -1;
  SilenceStderr() {
    saved_ = ::dup(STDERR_FILENO);
    if (saved_ < 0) return;
    devnull_ = ::open("/dev/null", O_WRONLY);
    if (devnull_ < 0) {
      ::close(saved_);
      saved_ = -1;
      return;
    }
    ::dup2(devnull_, STDERR_FILENO);
  }
  ~SilenceStderr() {
    if (saved_ >= 0) {
      ::dup2(saved_, STDERR_FILENO);
      ::close(saved_);
    }
    if (devnull_ >= 0) ::close(devnull_);
  }
  SilenceStderr(const SilenceStderr&) = delete;
  SilenceStderr& operator=(const SilenceStderr&) = delete;
};

}  // namespace

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
    for (size_t c = 0; c < num_channels_; ++c) out.at(c, f) = interleaved_[f * num_channels_ + c];
  io::save(out, path_.string());
  std::ostringstream ss;
  ss << "wrote " << path_ << "  frames=" << frames_ << "  duration=" << out.duration() << "s";
  SFERIC_LOG(Info, ss.str());
}

RealtimeSink::RealtimeSink(size_t num_channels, double sample_rate)
    : num_channels_(num_channels), sample_rate_(sample_rate) {
  try {
    SilenceStderr quiet;
    pa_check(Pa_Initialize(), "init");
    PaStream* stream = nullptr;
    pa_check(Pa_OpenDefaultStream(&stream, 0, static_cast<int>(num_channels_), paFloat32,
                                  sample_rate_, paFramesPerBufferUnspecified, nullptr, nullptr),
             "open");
    stream_ = stream;
    pa_check(Pa_StartStream(stream), "start");
  } catch (...) {
    if (stream_) Pa_CloseStream(static_cast<PaStream*>(stream_));
    Pa_Terminate();
    throw;
  }
  open_ = true;
  std::ostringstream ss;
  ss << "RealtimeSink open  channels=" << num_channels_ << "  sample_rate=" << sample_rate_;
  SFERIC_LOG(Info, ss.str());
}

RealtimeSink::~RealtimeSink() {
  if (open_) finalize();
}

void RealtimeSink::write_block(const AudioBuffer& block) {
  const size_t n = block.num_frames();
  const size_t ch = std::min(num_channels_, block.num_channels());
  interleaved_.assign(n * num_channels_, 0.0f);
  for (size_t f = 0; f < n; ++f)
    for (size_t c = 0; c < ch; ++c) interleaved_[f * num_channels_ + c] = block.at(c, f);
  pa_check(Pa_WriteStream(static_cast<PaStream*>(stream_), interleaved_.data(),
                          static_cast<unsigned long>(n)),
           "write");
}

void RealtimeSink::finalize() {
  if (!open_) return;
  PaStream* stream = static_cast<PaStream*>(stream_);
  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
  open_ = false;
  SFERIC_LOG(Info, "RealtimeSink closed");
}

}  // namespace sferic::io
