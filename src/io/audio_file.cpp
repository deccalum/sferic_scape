#include "io/audio_file.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#ifdef SFERIC_HAS_SNDFILE
#include <sndfile.h>
#endif

namespace sferic {
namespace io {

#ifdef SFERIC_HAS_SNDFILE

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
  if (dot == std::string::npos)
    throw std::runtime_error("Cannot determine format: no file extension");

  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == "wav") return SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  if (ext == "flac") return SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
  if (ext == "aiff" || ext == "aif") return SF_FORMAT_AIFF | SF_FORMAT_FLOAT;
  if (ext == "ogg") return SF_FORMAT_OGG | SF_FORMAT_VORBIS;

  throw std::runtime_error("Unsupported format: ." + ext);
}

AudioBuffer load(const std::string& path) {
  SF_INFO sf_info = {};
  SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sf_info);
  if (!file) {
    throw std::runtime_error("Failed to open audio file: " + path + " (" + sf_strerror(nullptr) +
                             ")");
  }

  auto channels = static_cast<size_t>(sf_info.channels);
  auto frames = static_cast<size_t>(sf_info.frames);
  double sr = static_cast<double>(sf_info.samplerate);

  AudioBuffer buffer(channels, frames, sr);

  // libsndfile reads interleaved, which matches our buffer layout
  std::vector<float> interleaved(channels * frames);
  sf_count_t read = sf_readf_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);

  if (static_cast<size_t>(read) != frames) {
    throw std::runtime_error("Short read: expected " + std::to_string(frames) + " frames, got " +
                             std::to_string(read));
  }

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
  if (!file) {
    throw std::runtime_error("Failed to open audio file: " + path + " (" + sf_strerror(nullptr) +
                             ")");
  }
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
  if (!file) {
    throw std::runtime_error("Failed to create audio file: " + path + " (" + sf_strerror(nullptr) +
                             ")");
  }

  // Build interleaved data
  size_t frames = buffer.num_frames();
  size_t channels = buffer.num_channels();
  std::vector<float> interleaved(channels * frames);

  for (size_t f = 0; f < frames; ++f) {
    for (size_t ch = 0; ch < channels; ++ch) {
      interleaved[f * channels + ch] = buffer.at(ch, f);
    }
  }

  sf_count_t written = sf_writef_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);

  if (static_cast<size_t>(written) != frames) {
    throw std::runtime_error("Short write: expected " + std::to_string(frames) + " frames, wrote " +
                             std::to_string(written));
  }
}

#else  // No libsndfile

AudioBuffer load(const std::string& path) {
  (void)path;
  throw std::runtime_error("sferic_scape built without libsndfile support");
}

AudioFileInfo info(const std::string& path) {
  (void)path;
  throw std::runtime_error("sferic_scape built without libsndfile support");
}

void save(const AudioBuffer& buffer, const std::string& path) {
  (void)buffer;
  (void)path;
  throw std::runtime_error("sferic_scape built without libsndfile support");
}

#endif

}  // namespace io
}  // namespace sferic
