#include "cli/commands.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <thread>

#include "analysis/analysis_processor.h"
#include "analysis/parametric_model.h"
#include "analysis/similarity.h"
#include "analysis/spectral_envelope.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "io/audio_file.h"
#include "io/data_domain.h"
#include "io/env.h"
#include "io/parametric_json.h"
#include "synthesis/noise_generator.h"
#include "synthesis/parametric_synth.h"

namespace fs = std::filesystem;

namespace sferic::cli {

using io::env::Artifact;

// Extract, serialise, and resynthesise one SpectralEnvelope, then score the parametric
// resynthesis against the envelope it came from — old vs new. `prefix` front-tags every
// output filename (versioned "new" set); `tag` back-tags the stem (e.g. ".raw").
static double emit(const analysis::SpectralEnvelope& spectral, const fs::path& group,
                   const std::string& stem, const std::string& version, const std::string& tag,
                   bool write_json) {
  analysis::ParametricModel model = analysis::ParametricExtractor().extract(spectral);

  if (write_json)
    io::save_parametric_json(
  model, io::env::artifact(group, stem, Artifact::Model, version, tag).string(), io::TrajectoryMode::Full);

  {
    synthesis::NoiseGenerator gen;
    gen.load_model(spectral);
    const size_t n = static_cast<size_t>(spectral.duration() * spectral.sample_rate);
    AudioBuffer out(1, n, spectral.sample_rate);
    gen.render(out.channel(0), n, 0.0);
    io::save(out, io::env::artifact(group, stem, Artifact::SpectralWav, version, tag).string());
  }

  synthesis::ParametricSynth synth;
  synth.load_model(model);
  AudioBuffer synth_out = synth.synthesize();
  io::save(synth_out, io::env::artifact(group, stem, Artifact::SynthWav, version, tag).string());

  const analysis::SpectralEnvelope resynth =
      analysis::SpectralAnalyzer(2048, 0, 15).analyze(synth_out);
  return analysis::envelope_similarity(spectral, resynth);
}

// Reveal the score after a brief pause.
static void show_score(const std::string& label, double pct) {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::printf("\n\033[1m  %s — %.1f%% similar\033[0m\n\n", label.c_str(), pct);
  std::fflush(stdout);
  SFERIC_LOG(Info, label + " similarity " + std::to_string(pct) + "%");
}

// Always ask when outputs exist. Returns the version suffix: empty to overwrite in
// place, or "_<timestamp>" to write a new versioned set alongside the old.
static std::string resolve_conflict(const fs::path& group, const std::string& stem) {
  if (!fs::exists(io::env::artifact(group, stem, Artifact::SynthWav))) return "";
  std::printf("output exists for '%s' — [o]verwrite / [n]ew? ", stem.c_str());
  std::fflush(stdout);
  std::string ans;
  std::getline(std::cin, ans);
  if (!ans.empty() && (ans[0] == 'n' || ans[0] == 'N')) return "_" + io::env::timestamp_tag();
  return "";
}

// skips files containing "<folder_name>__" unless pointed to exactly.
static bool is_source(const fs::path& wav) {
  const std::string folder = wav.parent_path().filename().string();
  return wav.stem().string().starts_with(folder + "__");
}

static void analyze_file(const fs::path& wav, const AnalyzeOptions& opts,
                         const io::LibraryEntry* entry) {
  SFERIC_SCOPE("cli::analyze_file");
  SFERIC_LOG(Info, "wav: " + wav.string());

  // Output groups by origin — the recording the segment came from (its containing folder).
  const std::string stem    = wav.stem().string();
  const fs::path group      = io::env::origin_dir(wav.parent_path().filename());
  const std::string version = resolve_conflict(group, stem);

  AudioBuffer audio = io::load(wav.string());

  // process:true entries are pre-processed — locate the user's spectral erasures against the
  // source and heal them before extraction, so the model never sees the silence.
  if (entry && entry->process && !entry->source.empty()) {
    SFERIC_LOG(
      Info, "pre-process: source=" + entry->source + " start="
      + std::to_string(entry->segment_start_s) + "s");

    analysis::LocateConfig cfg;
    cfg.debug_dump_path = io::env::artifact(group, stem, Artifact::LocateDump, version).string();
    analysis::AnalysisProcessor proc(cfg);

    AudioBuffer source = io::load(entry->source);
    analysis::PreprocessResult pre = proc.preprocess(audio, source, entry->segment_start_s);

    show_score(stem + " (healed)", emit(pre.healed, group, stem, version, "", opts.write_json));
    if (opts.emit_unhealed)
      show_score(stem + " (raw)", emit(pre.raw, group, stem, version, ".raw", opts.write_json));
    return;
  }

  show_score(
    stem, emit(analysis::SpectralAnalyzer(2048, 0, 15).analyze(audio),
    group, stem, version, "", opts.write_json));
}

void analyze(const std::filesystem::path& input, const AnalyzeOptions& opts) {
  SFERIC_SCOPE("cli::analyze");

  // The library (if a root is known) supplies per-file process/source metadata and the set
  // of source recordings to skip. Directory input is its own root; otherwise use wav_root.
  const fs::path root = fs::is_directory(input) ? input : io::env::wav_root();
  std::map<fs::path, io::LibraryEntry> by_path;
  std::set<fs::path> sources;
  if (fs::exists(root)) {
    for (const io::LibraryEntry& e : io::load_library(root)) {
      by_path.emplace(fs::weakly_canonical(e.wav_path), e);
      if (!e.source.empty()) sources.insert(fs::weakly_canonical(e.source));
    }
  }
  const auto lookup = [&](const fs::path& p) -> const io::LibraryEntry* {
    const auto it = by_path.find(fs::weakly_canonical(p));
    return it == by_path.end() ? nullptr : &it->second;
  };

  if (fs::is_directory(input)) {
    for (const auto& de : fs::recursive_directory_iterator(input)) {
      if (de.path().extension() != ".wav") continue;
      if (is_source(de.path()) || sources.count(fs::weakly_canonical(de.path()))) {
        SFERIC_LOG(Info, "skipping: " + de.path().filename().string());
        continue;
      }

      analyze_file(de.path(), opts, lookup(de.path()));
    }
    return;
  }

  analyze_file(input, opts, lookup(input));
}

}  // namespace sferic::cli
