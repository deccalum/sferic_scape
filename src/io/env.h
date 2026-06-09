#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace sferic {
namespace io {
namespace env {

// Load key=value lines from a dotenv file into the process environment. Existing
// variables are not overwritten. Call once at startup, before any accessor below.
void load(const std::filesystem::path& dotenv = ".env");

const std::filesystem::path& wav_root();  // SFERIC_WAV_ROOT — library / input root
const std::filesystem::path& out_dir();   // SFERIC_OUT_DIR  — generated artifacts root
const std::filesystem::path& log_dir();   // SFERIC_LOG_DIR  — log file directory

// Optional boolean toggle — true when the var is set to 1/true/yes/on, false if unset.
bool flag(const char* key);

// The closed set of files written for one analyzed segment.
enum class Artifact : uint8_t {
  Model,        // <stem>jsonmodel.json — parametric model (opt-in)
  SpectralWav,  // <stem>_spectral.wav  — direct envelope reconstruction
  SynthWav,     // <stem>_synth.wav     — parametric model resynthesis
  LocateDump,   // <stem>_locate.bin    — debug planes for plot_locate.py
};

// Output directory for one recording origin — out_dir()/origin. Created on call.
std::filesystem::path origin_dir(const std::filesystem::path& origin);

// Path to one artifact for segment `stem` inside its origin `group`. Every output is
// prefixed with the segment stem so a recording's segments share one flat directory.
// `version` separates a re-run from an overwrite (e.g. "_20260609_120000"); `tag` marks a
// variant (e.g. ".raw"). Model → "<stem><version><tag>jsonmodel.json";
// others → "<stem><version>_<base><tag>.<ext>".
std::filesystem::path artifact(const std::filesystem::path& group, const std::string& stem,
                               Artifact a, const std::string& version = "",
                               const std::string& tag = "");

// Filesystem-safe timestamp (YYYYMMDD_HHMMSS) for versioned output filenames.
std::string timestamp_tag();

}  // namespace env
}  // namespace io
}  // namespace sferic
