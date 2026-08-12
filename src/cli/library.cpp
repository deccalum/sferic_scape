#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/browse.h"
#include "cli/commands.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "io/audio_file.h"
#include "ui/term.h"
#include "ui/text.h"

namespace sferic::cli {
namespace {

template <typename... Args>
std::string line(const char* fmt, Args... args) {
  char buf[192];
  std::snprintf(buf, sizeof buf, fmt, args...);
  return std::string(buf);
}

std::optional<std::optional<CorpusRole>> pick_corpus_filter() {
  const std::vector<std::string> items{"all", "reference", "curated"};
  const std::optional<size_t> choice = ui::select("filter by corpus", items);
  if (!choice) return std::nullopt;
  switch (*choice) {
    case 0:
      return std::optional<CorpusRole>(std::nullopt);
    case 1:
      return std::optional<CorpusRole>(CorpusRole::Reference);
    case 2:
      return std::optional<CorpusRole>(CorpusRole::Curated);
  }
  return std::nullopt;
}

std::optional<ReviewSet> pick_review_set() {
  const std::vector<std::string> items{"pending", "approved", "rejected"};
  const std::optional<size_t> choice = ui::select("which candidates", items);
  if (!choice) return std::nullopt;
  switch (*choice) {
    case 0:
      return ReviewSet::Pending;
    case 1:
      return ReviewSet::Approved;
    case 2:
      return ReviewSet::Rejected;
  }
  return std::nullopt;
}

void browse_recordings(io::MediaRepository& repo, Domain domain, std::optional<CorpusRole> role,
                       const ui::AudioSinkFactory& make_sink) {
  const std::vector<io::MediaSourceInfo> rows = repo.fetch_recordings(domain, role);
  int64_t cached = -1;
  AudioBuffer audio;
  ui::Player player(make_sink);
  const auto name_of = [](const io::MediaSourceInfo& m) {
    return m.origin == "freesound" ? m.external_id : (m.title.empty() ? m.external_id : m.title);
  };
  const auto label = [&](const io::MediaSourceInfo& m) {
    return line("%s %s %s", ui::pad(ui::trunc(name_of(m), 40), 40).c_str(),
                ui::pad(ui::fmt_len(m.length_s), 8).c_str(),
                ui::pad(to_string(m.corpus_role), 9).c_str());
  };
  const auto play_row = [&](const io::MediaSourceInfo& m) {
    if (m.id != cached) {
      const std::vector<std::byte> bytes = repo.fetch_media_audio(m.id);
      audio = io::decode(std::span<const std::byte>(bytes));
      cached = m.id;
    }
    std::printf("  playing %s …\n", name_of(m).c_str());
    std::fflush(stdout);
    player.play(audio);
  };
  const auto info_row = [](const io::MediaSourceInfo& m) {
    std::printf("  media %lld  origin %s  external_id %s  role %s\n", static_cast<long long>(m.id),
                m.origin.c_str(), m.external_id.c_str(),
                std::string(to_string(m.corpus_role)).c_str());
    if (!m.title.empty()) std::printf("  title: %s\n", m.title.c_str());
    if (!m.recorded_by.empty()) std::printf("  by: %s\n", m.recorded_by.c_str());
    std::printf("  %s\n", m.url.c_str());
    if (m.length_s > 0.0) std::printf("  segment length %s\n", ui::fmt_len(m.length_s).c_str());
  };

  ui::browse_playable("recordings · " + std::string(to_string(domain)), rows, label, play_row,
                      info_row);
}

void browse_samples(io::MediaRepository& repo, Domain domain, std::optional<CorpusRole> role,
                    const ui::AudioSinkFactory& make_sink) {
  const std::vector<io::ProcessedRow> rows = repo.fetch_processed(domain, role);
  ui::Player player(make_sink);
  const auto label = [](const io::ProcessedRow& p) {
    return line("%s %s conf %.2f  %s", ui::pad(ui::trunc(p.external_id, 20), 20).c_str(),
                ui::pad(ui::fmt_len(p.duration_s), 8).c_str(), p.confidence,
                ui::pad(to_string(p.corpus_role), 9).c_str());
  };
  const auto play_row = [&](const io::ProcessedRow& p) {
    const std::vector<std::byte> bytes = repo.fetch_processed_audio(p.id);
    const AudioBuffer audio = io::decode(std::span<const std::byte>(bytes));
    std::printf("  playing sample %lld …\n", static_cast<long long>(p.id));
    std::fflush(stdout);
    player.play(audio);
  };
  const auto info_row = [&](const io::ProcessedRow& p) {
    std::printf("  %s\n", p.external_id.c_str());
    if (p.title) std::printf("  title: %s\n", p.title->c_str());
    if (p.recorded_by) std::printf("  by: %s\n", p.recorded_by->c_str());
    std::printf("  sample %lld  from media %lld  window %.2f–%.2fs\n", static_cast<long long>(p.id),
                static_cast<long long>(p.source_media_id), p.start_s, p.end_s);
    std::printf("  duration %s  conf %.3f  peak %.3f  noise floor %.1f dB  %lld bytes\n",
                ui::fmt_len(p.duration_s).c_str(), p.confidence, p.peak, p.noise_floor,
                static_cast<long long>(p.size_bytes));
    if (p.url) std::printf("  %s\n", p.url->c_str());
    if (const std::optional<double> score = repo.fetch_resynth_score(p.id))
      std::printf("  resynth similarity: %.1f%% (analyzed)\n", *score);
    else
      std::printf("  resynth similarity: — (not analyzed)\n");
    if (const std::optional<io::AdsrRow> a = repo.fetch_sample_adsr(p.id))
      std::printf("  adsr: attack %.3fs  peak %.3fs  release %.3fs  sustain %.2f\n", a->attack_s,
                  a->peak_s, a->release_s, a->sustain_level);
    const std::vector<io::BandRow> bands = repo.fetch_sample_bands(p.id);
    std::printf("  %zu bands:\n", bands.size());
    for (const io::BandRow& b : bands)
      std::printf("    [%d] %7.1f–%7.1f Hz  %.3f\n", b.band_index, b.f_lo_hz, b.f_hi_hz, b.amount);
  };
  std::vector<ui::RowAction<io::ProcessedRow>> extra;
  if (role == CorpusRole::Curated)
    extra.push_back(
        {'a', "[a]nalyze", [&repo](const io::ProcessedRow& p) {
           const std::vector<std::byte> bytes = repo.fetch_processed_audio(p.id);
           const AudioBuffer sample = io::decode(std::span<const std::byte>(bytes));
           std::printf("  analyzing sample %lld — resynthesis scoring, this can take a while…\n",
                       static_cast<long long>(p.id));
           std::fflush(stdout);
           const bool prev = log::echo_stdout();
           log::set_echo_stdout(true);
           const double pct = analyze_sample(sample, "sample " + std::to_string(p.id));
           repo.store_resynth_score(p.id, pct);
           log::set_echo_stdout(prev);
         }});

  ui::browse_playable("samples · " + std::string(to_string(domain)), rows, label, play_row,
                      info_row, extra);
}

void browse_raw_tables() {
  std::vector<std::string> names;
  for (const auto& [name, _] : table_registry) names.emplace_back(name);
  std::ranges::sort(names);
  const std::optional<size_t> choice = ui::select("raw table dump", names);
  if (!choice) return;
  table_registry.at(names[*choice])->fetch_and_display();
  ui::wait_for_key("\n\033[90mpress any key to go back\033[0m");
}

}  // namespace

void library(io::MediaRepository& repo, Domain domain, const ui::AudioSinkFactory& make_sink) {
  SFERIC_SCOPE("cli::library");
  ui::MuteLogEcho mute_echo;

  while (true) {
    const io::LibraryCounts c = repo.count_library(domain);
    const std::vector<std::string> items{
        line("Recordings     reference %d · curated %d", c.reference_media, c.curated_media),
        line("Candidates  pending %d · approved %d · rejected %d", c.pending, c.approved,
             c.rejected),
        line("Samples        extracted %d", c.samples),
        line("Raw tables     %zu registered", table_registry.size()),
    };
    const std::optional<size_t> choice =
        ui::select("library · " + std::string(to_string(domain)), items);
    if (!choice) return;

    switch (*choice) {
      case 0:
        if (const std::optional<std::optional<CorpusRole>> f = pick_corpus_filter())
          browse_recordings(repo, domain, *f, make_sink);
        break;
      case 1:
        if (const std::optional<ReviewSet> s = pick_review_set())
          review(repo, domain, *s, make_sink);
        break;
      case 2:
        if (const std::optional<std::optional<CorpusRole>> f = pick_corpus_filter())
          browse_samples(repo, domain, *f, make_sink);
        break;
      case 3:
        browse_raw_tables();
        break;
    }
  }
}

void analyze_samples(io::MediaRepository& repo, Domain domain,
                     const ui::AudioSinkFactory& make_sink) {
  SFERIC_SCOPE("cli::analyze_samples");
  ui::MuteLogEcho mute_echo;
  browse_samples(repo, domain, CorpusRole::Curated, make_sink);
}

}  // namespace sferic::cli
