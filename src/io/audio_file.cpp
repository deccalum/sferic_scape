#include "io/audio_file.h"

#include <sndfile.h>

#include <algorithm>
#include <string>
#include <utility>


namespace sferic {
namespace io {

static std::string format_name(int format) {
  switch (format & SF_FORMAT_TYPEMASK) {
    case SF_FORMAT_WAV:
      return "WAV";
    case SF_FORMAT_FLAC:
      return "FLAC";
    case SF_FORMAT_AIFF:
      return "AIFF";
    case SF_FORMAT_OGG:
      return "OGG";
    default:
      return "Unknown";
  }
}

static int format_from_extension(const std::string& path) {
  auto dot = path.rfind('.');
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == "wav") return SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  if (ext == "flac") return SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
  if (ext == "aiff" || ext == "aif") return SF_FORMAT_AIFF | SF_FORMAT_FLOAT;
  if (ext == "ogg") return SF_FORMAT_OGG | SF_FORMAT_VORBIS;
  std::unreachable();
}

AudioBuffer load(const std::string& path) {
  SF_INFO sf_info = {};
  SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sf_info);

  auto channels = static_cast<size_t>(sf_info.channels);
  auto frames = static_cast<size_t>(sf_info.frames);
  double sr = static_cast<double>(sf_info.samplerate);

  AudioBuffer buffer(channels, frames, sr);

  // libsndfile reads interleaved, which matches our buffer layout
  std::vector<float> interleaved(channels * frames);
  sf_readf_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);

  // Copy into buffer
  for (size_t f = 0; f < frames; ++f) {
    for (size_t ch = 0; ch < channels; ++ch) {
      buffer.at(ch, f) = interleaved[f * channels + ch];
    }
  }

  return buffer;
}

AudioFileInfo info(const std::string& path) {
  SF_INFO sf_info = {};
  SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sf_info);
  sf_close(file);

  return AudioFileInfo{
      static_cast<size_t>(sf_info.channels), static_cast<size_t>(sf_info.frames),
      static_cast<double>(sf_info.samplerate),
      static_cast<double>(sf_info.frames) / static_cast<double>(sf_info.samplerate),
      format_name(sf_info.format)};
}

void save(const AudioBuffer& buffer, const std::string& path) {
  SF_INFO sf_info = {};
  sf_info.channels = static_cast<int>(buffer.num_channels());
  sf_info.samplerate = static_cast<int>(buffer.sample_rate());
  sf_info.format = format_from_extension(path);

  SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sf_info);

  // Build interleaved data
  size_t frames = buffer.num_frames();
  size_t channels = buffer.num_channels();
  std::vector<float> interleaved(channels * frames);

  for (size_t f = 0; f < frames; ++f) {
    for (size_t ch = 0; ch < channels; ++ch) {
      interleaved[f * channels + ch] = buffer.at(ch, f);
    }
  }

  sf_writef_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);
}

}  // namespace io
}  // namespace sferic
