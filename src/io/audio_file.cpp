#include "io/audio_file.h"

#include <sndfile.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "core/logger.h"

namespace sferic::io {
namespace {

struct MemoryReader {
  std::span<const std::byte> data;  // FLAC (or other) bytes to decode
  sf_count_t pos = 0;               // current read offset for sf_virtual_io
};

sf_count_t vio_get_filelen(void* user) {
  return static_cast<sf_count_t>(static_cast<MemoryReader*>(user)->data.size());
}

sf_count_t vio_seek(sf_count_t offset, int whence, void* user) {
  auto* r = static_cast<MemoryReader*>(user);
  const sf_count_t size = static_cast<sf_count_t>(r->data.size());
  sf_count_t base = 0;
  switch (whence) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      base = r->pos;
      break;
    case SEEK_END:
      base = size;
      break;
    default:
      std::unreachable();
  }
  r->pos = base + offset;
  return r->pos;
}

sf_count_t vio_read(void* ptr, sf_count_t count, void* user) {
  auto* r = static_cast<MemoryReader*>(user);
  const sf_count_t remaining = static_cast<sf_count_t>(r->data.size()) - r->pos;
  const sf_count_t n = std::min(count, remaining < 0 ? 0 : remaining);
  if (n > 0) {
    std::memcpy(ptr, r->data.data() + r->pos, static_cast<size_t>(n));
    r->pos += n;
  }
  return n;
}

sf_count_t vio_write(const void*, sf_count_t, void*) { return 0; }
sf_count_t vio_tell(void* user) { return static_cast<MemoryReader*>(user)->pos; }

struct MemoryWriter {
  std::vector<std::byte> data;  // growing FLAC output
  sf_count_t pos = 0;           // current write offset for sf_virtual_io
};

sf_count_t w_get_filelen(void* user) {
  return static_cast<sf_count_t>(static_cast<MemoryWriter*>(user)->data.size());
}

sf_count_t w_seek(sf_count_t offset, int whence, void* user) {
  auto* w = static_cast<MemoryWriter*>(user);
  sf_count_t base = 0;
  switch (whence) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      base = w->pos;
      break;
    case SEEK_END:
      base = static_cast<sf_count_t>(w->data.size());
      break;
    default:
      std::unreachable();
  }
  w->pos = base + offset;
  return w->pos;
}

sf_count_t w_read(void* ptr, sf_count_t count, void* user) {
  auto* w = static_cast<MemoryWriter*>(user);
  const sf_count_t remaining = static_cast<sf_count_t>(w->data.size()) - w->pos;
  const sf_count_t n = std::min(count, remaining < 0 ? 0 : remaining);
  if (n > 0) {
    std::memcpy(ptr, w->data.data() + w->pos, static_cast<size_t>(n));
    w->pos += n;
  }
  return n;
}

sf_count_t w_write(const void* ptr, sf_count_t count, void* user) {
  auto* w = static_cast<MemoryWriter*>(user);
  if (w->pos + count > static_cast<sf_count_t>(w->data.size()))
    w->data.resize(static_cast<size_t>(w->pos + count));
  std::memcpy(w->data.data() + w->pos, ptr, static_cast<size_t>(count));
  w->pos += count;
  return count;
}

sf_count_t w_tell(void* user) { return static_cast<MemoryWriter*>(user)->pos; }

}  // namespace

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
  std::vector<float> interleaved(channels * frames);
  sf_readf_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);
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

AudioBuffer decode(std::span<const std::byte> bytes) {
  SFERIC_SCOPE("decode");

  MemoryReader reader{bytes};
  SF_VIRTUAL_IO vio{vio_get_filelen, vio_seek, vio_read, vio_write, vio_tell};
  SF_INFO sf_info = {};
  SNDFILE* file = sf_open_virtual(&vio, SFM_READ, &sf_info, &reader);
  auto channels = static_cast<size_t>(sf_info.channels);
  auto frames = static_cast<size_t>(sf_info.frames);
  double sr = static_cast<double>(sf_info.samplerate);
  AudioBuffer buffer(channels, frames, sr);
  std::vector<float> interleaved(channels * frames);
  sf_readf_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);
  for (size_t f = 0; f < frames; ++f) {
    for (size_t ch = 0; ch < channels; ++ch) {
      buffer.at(ch, f) = interleaved[f * channels + ch];
    }
  }

  SFERIC_LOG(Info, "decoded " + std::to_string(bytes.size()) + " bytes -> " +
                       std::to_string(frames) + " frames @ " + std::to_string(sr) + "Hz");
  return buffer;
}

std::vector<std::byte> encode_flac(const AudioBuffer& buffer) {
  SFERIC_SCOPE("encode_flac");

  MemoryWriter writer;
  SF_VIRTUAL_IO vio{w_get_filelen, w_seek, w_read, w_write, w_tell};
  SF_INFO sf_info = {};
  sf_info.channels = static_cast<int>(buffer.num_channels());
  sf_info.samplerate = static_cast<int>(buffer.sample_rate());
  sf_info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
  SNDFILE* file = sf_open_virtual(&vio, SFM_WRITE, &sf_info, &writer);
  const size_t frames = buffer.num_frames();
  const size_t channels = buffer.num_channels();
  std::vector<float> interleaved(channels * frames);
  for (size_t f = 0; f < frames; ++f)
    for (size_t ch = 0; ch < channels; ++ch) interleaved[f * channels + ch] = buffer.at(ch, f);
  sf_writef_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
  sf_close(file);

  SFERIC_LOG(Info, "encoded " + std::to_string(frames) + " frames @ " +
                       std::to_string(buffer.sample_rate()) + "Hz -> " +
                       std::to_string(writer.data.size()) + " FLAC bytes");
  return std::move(writer.data);
}

void save(const AudioBuffer& buffer, const std::string& path) {
  SF_INFO sf_info = {};
  sf_info.channels = static_cast<int>(buffer.num_channels());
  sf_info.samplerate = static_cast<int>(buffer.sample_rate());
  sf_info.format = format_from_extension(path);
  SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sf_info);
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

}  // namespace sferic::io
