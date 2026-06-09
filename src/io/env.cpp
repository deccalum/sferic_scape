#include "io/env.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sferic {
namespace io {
namespace env {

namespace {

std::string require(const char* key) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : throw std::runtime_error(std::string("missing env var: ") + key);
}

struct ArtifactName {
  const char* base;
  const char* ext;
  bool tagged; 
};

ArtifactName name_of(Artifact a) {
  switch (a) {
    case Artifact::Model:    return {"model", ".json", true};
    case Artifact::SpectralWav:  return {"spectral", ".wav", true};
    case Artifact::SynthWav:     return {"synth", ".wav", true};
    case Artifact::LocateDump:   return {"locate", ".bin", false};
  }
  return {"artifact", ".bin", false};
}

}  // namespace

void load(const std::filesystem::path& dotenv) {
  std::ifstream f(dotenv);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    ::setenv(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), 0);
  }
}

const std::filesystem::path& wav_root() {
  static const std::filesystem::path p = require("SFERIC_WAV_ROOT");
  return p;
}

const std::filesystem::path& out_dir() {
  static const std::filesystem::path p = require("SFERIC_OUT_DIR");
  return p;
}

const std::filesystem::path& log_dir() {
  static const std::filesystem::path p = require("SFERIC_LOG_DIR");
  return p;
}

bool flag(const char* key) {
  const char* v = std::getenv(key);
  if (!v) return false;
  const std::string s(v);
  return s == "1" || s == "true" || s == "yes" || s == "on";
}

std::filesystem::path origin_dir(const std::filesystem::path& origin) {
  const std::filesystem::path g = out_dir() / origin;
  std::filesystem::create_directories(g);
  return g;
}

std::filesystem::path artifact(const std::filesystem::path& group, const std::string& stem,
                               Artifact a, const std::string& version, const std::string& tag) {
  if (a == Artifact::Model)
    return group / (stem + version + tag + "jsonmodel.json");

  const ArtifactName n = name_of(a);
  const std::string fname =
      stem + version + "_" + n.base + (n.tagged ? tag : std::string()) + n.ext;
  return group / fname;
}

std::string timestamp_tag() {
  const auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm_buf{};
  localtime_r(&now_t, &tm_buf);
  std::ostringstream ss;
  ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
  return ss.str();
}

}  // namespace env
}  // namespace io
}  // namespace sferic
