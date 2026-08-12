#include "cli/commands.h"

#include <sys/wait.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include "analysis/parametric_model.h"
#include "analysis/similarity.h"
#include "analysis/spectral_envelope.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "io/audio_file.h"
#include "io/env.h"
#include "synthesis/parametric_synth.h"

namespace sferic::cli {

int ingest(Domain domain) {
  SFERIC_SCOPE("cli::ingest");

  const std::string cmd = io::env::python_interpreter() +
                          " -m ingest --source freesound --domain " +
                          std::string(to_string(domain));
  SFERIC_LOG(Info, "ingest: " + cmd);

  const int status = std::system(cmd.c_str());
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  SFERIC_LOG(Info, "ingest exited with code " + std::to_string(code));
  return code;
}

static double score_resynthesis(const analysis::SpectralEnvelope& spectral) {
  const analysis::ParametricModel model = analysis::ParametricExtractor().extract(spectral);

  synthesis::ParametricSynth synth;
  synth.load_model(model);
  const AudioBuffer synth_out = synth.synthesize();

  const analysis::SpectralEnvelope resynth =
      analysis::SpectralAnalyzer(2048, 0, 15).analyze(synth_out);
  return analysis::envelope_similarity(spectral, resynth);
}

static void show_score(const std::string& label, double pct) {
  std::printf("\n\033[1m  %s — %.1f%% similar\033[0m\n\n", label.c_str(), pct);
  std::fflush(stdout);
  SFERIC_LOG(Info, label + " similarity " + std::to_string(pct) + "%");
}

double analyze_sample(const AudioBuffer& sample, const std::string& label) {
  SFERIC_SCOPE("cli::analyze_sample");
  const double pct = score_resynthesis(analysis::SpectralAnalyzer(2048, 0, 15).analyze(sample));
  show_score(label, pct);
  return pct;
}

void analyze(io::MediaRepository& repo, Domain domain, const AnalyzeOptions&) {
  SFERIC_SCOPE("cli::analyze");

  const std::vector<io::ProcessedRow> samples = repo.fetch_processed(domain, CorpusRole::Curated);
  if (samples.empty()) {
    SFERIC_LOG(Warn, "no curated samples for domain=" + std::string(to_string(domain)) +
                         " — extract curated samples first (detect -> review -> extract); "
                         "nothing to analyze");
    return;
  }

  for (size_t i = 0; i < samples.size(); ++i) {
    const io::ProcessedRow& s = samples[i];
    const std::vector<std::byte> bytes = repo.fetch_processed_audio(s.id);
    const AudioBuffer sample = io::decode(std::span<const std::byte>(bytes));
    const double pct = analyze_sample(sample, "sample " + std::to_string(s.id));
    repo.store_resynth_score(s.id, pct);
    SFERIC_PROGRESS(i + 1, samples.size(), sample.duration() * 1000.0, bytes.size());
  }
}

}  // namespace sferic::cli
