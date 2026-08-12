#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analysis/prepare.h"
#include "cli/commands.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "io/audio_file.h"
#include "ui/term.h"

namespace sferic::cli {
namespace {

std::vector<io::ExemplarCandidate> load_set(io::MediaRepository& repo, Domain domain,
                                            ReviewSet set) {
  switch (set) {
    case ReviewSet::Pending:
      return repo.fetch_unreviewed(domain);
    case ReviewSet::Approved:
      return repo.fetch_approved(domain);
    case ReviewSet::Rejected:
      return repo.fetch_rejected(domain);
  }
  std::unreachable();
}

const char* empty_hint(ReviewSet set) {
  switch (set) {
    case ReviewSet::Pending:
      return "nothing pending";
    case ReviewSet::Approved:
      return "nothing approved";
    case ReviewSet::Rejected:
      return "nothing rejected";
  }
  std::unreachable();
}

std::string fmt_len(double seconds) {
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(seconds < 10.0 ? 2 : 1) << seconds << "s";
  return ss.str();
}

std::string fmt_part(const io::MediaSourceInfo* m) {
  if (!m || m->parts <= 1) return "-";
  return std::to_string(m->part) + "/" + std::to_string(m->parts);
}

void print_candidate_summary(size_t i, size_t n, const io::ExemplarCandidate& c,
                             const io::MediaSourceInfo* src) {
  const std::string title = src ? (src->title.empty() ? src->external_id : src->title) : "...";
  const std::string url = src ? src->url : "?";
  const double len = c.end_s > c.start_s ? c.end_s - c.start_s : 0.0;
  std::printf("\n[%zu/%zu]  %s\n", i + 1, n, title.c_str());
  std::printf("  %s\n", url.c_str());
  std::printf("  part %s   length %s\n", fmt_part(src).c_str(), fmt_len(len).c_str());
  if (c.reject_reason)
    std::printf("  rejected: %s\n", std::string(to_string(*c.reject_reason)).c_str());
}

void print_advanced(const io::ExemplarCandidate& c, const io::MediaSourceInfo* src) {
  std::printf("  [ADVANCED]\n");
  std::printf("  exemplar id: %lld  media id: %lld\n", static_cast<long long>(c.id),
              static_cast<long long>(c.source_media_id));
  std::printf("  window %.3f–%.3fs  conf: %.3f\n", c.start_s, c.end_s, c.confidence);
  if (src) {
    std::printf("  origin: %s  external_id: %s  role: %s\n", src->origin.c_str(),
                src->external_id.c_str(), std::string(to_string(src->corpus_role)).c_str());
    if (src->start_s || src->end_s) {
      std::printf("  media segment ");
      if (src->start_s)
        std::printf("%.1f", *src->start_s);
      else
        std::printf("?");
      std::printf("–");
      if (src->end_s)
        std::printf("%.1f", *src->end_s);
      else
        std::printf("?");
      std::printf("s  (clip length %s)\n", fmt_len(src->length_s).c_str());
    }
  }
}

std::optional<RejectReason> prompt_reject_reason() {
  std::printf(
      "  [REJECT REASON]\n"
      "  [1]noise [2]unsort [3]bad window [4]bad quality [esc]back\n");
  std::fflush(stdout);
  while (true) {
    switch (ui::read_key()) {
      case '1':
        return RejectReason::Noise;
      case '2':
        return RejectReason::Unsort;
      case '3':
        return RejectReason::BadWindow;
      case '4':
        return RejectReason::BadQuality;
      case 'q':
      case 27:
        return std::nullopt;
    }
  }
}

}  // namespace

void review(io::MediaRepository& repo, Domain domain, ReviewSet set,
            const ui::AudioSinkFactory& make_sink) {
  SFERIC_SCOPE("cli::review");

  const std::vector<io::ExemplarCandidate> candidates = load_set(repo, domain, set);
  if (candidates.empty()) {
    SFERIC_LOG(Warn, "no " + std::string(to_string(set)) + " candidates for domain: " +
                         std::string(to_string(domain)) + " — " + empty_hint(set));
    return;
  }

  const std::vector<io::MediaSourceInfo> catalog = repo.fetch_media_catalog();
  std::unordered_map<int64_t, const io::MediaSourceInfo*> by_media;
  by_media.reserve(catalog.size());
  for (const io::MediaSourceInfo& m : catalog) by_media[m.id] = &m;
  ui::MuteLogEcho mute_echo;
  std::printf(
      "\033[2J\033[H"
      "review %s [%s] — %zu candidates\n"
      "[space]replay [a]pprove [r]eject [s]kip [i]nfo [q]uit\n",
      std::string(to_string(domain)).c_str(), std::string(to_string(set)).c_str(),
      candidates.size());
  std::fflush(stdout);
  int64_t cached_media_id = -1;
  AudioBuffer media;
  ui::RawMode raw;
  ui::Player player(make_sink);

  for (size_t i = 0; i < candidates.size(); ++i) {
    const io::ExemplarCandidate& c = candidates[i];
    const io::MediaSourceInfo* src =
        by_media.count(c.source_media_id) ? by_media[c.source_media_id] : nullptr;
    if (c.source_media_id != cached_media_id) {
      const std::vector<std::byte> bytes = repo.fetch_media_audio(c.source_media_id);
      media = io::decode(std::span<const std::byte>(bytes));
      cached_media_id = c.source_media_id;
    }
    const AudioBuffer seg = analysis::prepare::cut(media, c.start_s, c.end_s);
    print_candidate_summary(i, candidates.size(), c, src);
    std::printf("  playing…\n");
    std::fflush(stdout);
    player.play(seg);
    bool decided = false;
    while (!decided) {
      std::printf("[space]replay [a]pprove [r]eject [s]kip [i]nfo [q]uit\n");
      std::fflush(stdout);
      switch (ui::read_key()) {
        case ' ':
          std::printf("  playing…\n");
          std::fflush(stdout);
          player.play(seg);
          break;
        case 'i':
          print_advanced(c, src);
          break;
        case 'a':
        case '\r':
        case '\n':
          repo.approve_exemplar(c.id);
          std::printf("  approved\n");
          decided = true;
          break;
        case 'r':
          if (const std::optional<RejectReason> reason = prompt_reject_reason()) {
            repo.reject_exemplar(c.id, *reason);
            std::printf("  rejected: %s\n", std::string(to_string(*reason)).c_str());
            decided = true;
          }
          break;
        case 's':
        case ui::Key::Right:
          std::printf("  skipped\n");
          decided = true;
          break;
        case 'q':
        case 27:
          std::printf("  quit\n");
          return;
      }
    }
  }

  SFERIC_LOG(Info, "end of review" + std::string(to_string(domain)) + std::string(to_string(set)));
}

}  // namespace sferic::cli
