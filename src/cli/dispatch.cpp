#include "cli/dispatch.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cli/browse.h"
#include "module/thunder/detector.h"
#include "ui/term.h"

namespace sferic::cli {
namespace {

constexpr std::array kDomains{Domain::Thunder, Domain::Rain, Domain::Wind, Domain::Ocean};

std::optional<Domain> pick_domain() {
  std::vector<std::string> items;
  for (Domain d : kDomains) items.emplace_back(to_string(d));
  const std::optional<size_t> choice = ui::select("choose a domain", items);
  return choice ? std::optional(kDomains[*choice]) : std::nullopt;
}

std::optional<DetectOptions> pick_detect_opts() {
  const std::vector<std::string> items{"reference corpus  — validate the detector in bulk",
                                       "curated corpus    — the samples a model is built from"};
  const std::optional<size_t> choice = ui::select("detect — which corpus", items);
  if (!choice) return std::nullopt;
  DetectOptions opts;
  opts.corpus = *choice == 1 ? CorpusRole::Curated : CorpusRole::Reference;
  return opts;
}

}  // namespace

std::unique_ptr<analysis::IDetector> make_detector(Domain domain) {
  switch (domain) {
    case Domain::Thunder:
      return std::make_unique<thunder::ThunderDetector>(thunder::DetectorConfig{});
    case Domain::Rain:
    case Domain::Wind:
    case Domain::Ocean:
      throw std::runtime_error("no detector implemented for domain: " +
                               std::string(to_string(domain)));
  }
  std::unreachable();
}

void run(Command command, io::MediaRepository& repo, Domain domain, DetectOptions detect_opts) {
  switch (command) {
    case Command::Detect: {
      const std::unique_ptr<analysis::IDetector> detector = make_detector(domain);
      walk_corpus(repo, domain, *detector, detect_opts);
      break;
    }
    case Command::Analyze:
      analyze(repo, domain, AnalyzeOptions{});
      break;
    case Command::Clear:
      repo.clear_unreviewed(domain);
      break;
    case Command::Ingest:
    case Command::Extract:
    case Command::Review:
    case Command::Browse:
      break;
  }
}

void run_browse(std::string_view table) { table_registry.at(table)->fetch_and_display(); }

int dispatch(const Invocation& invocation, io::MediaRepository& repo,
             const ui::AudioSinkFactory& make_sink) {
  const CommandSpec& spec = *invocation.spec;

  if (spec.command == Command::Browse) {
    run_browse(invocation.table);
    return 0;
  }
  const Domain domain = *invocation.domain;
  if (spec.command == Command::Review) {
    review(repo, domain, invocation.review_set, make_sink);
    return 0;
  }
  if (spec.command == Command::Extract) {
    extract(repo, domain, invocation.extract_opts);
    return 0;
  }
  if (spec.command == Command::Ingest) {
    return ingest(domain);
  }
  run(spec.command, repo, domain, invocation.detect_opts);
  return 0;
}

void run_interactive(io::MediaRepository& repo, const ui::AudioSinkFactory& make_sink) {
  std::optional<Domain> domain = pick_domain();
  if (!domain) return;
  while (true) {
    const std::string dom = std::string(to_string(*domain));
    const std::vector<std::string> items{
        "Library", "Ingest", "Detect", "Extract", "Analyze", "Switch domain (" + dom + ")", "Quit",
    };
    const std::optional<size_t> choice = ui::select("sferic · " + dom, items);
    if (!choice) return;

    bool ran = false;
    switch (*choice) {
      case 0:
        library(repo, *domain, make_sink);
        break;
      case 1:
        ingest(*domain);
        ran = true;
        break;
      case 2:
        if (const std::optional<DetectOptions> opts = pick_detect_opts()) {
          run(Command::Detect, repo, *domain, *opts);
          ran = true;
        }
        break;
      case 3:
        extract(repo, *domain, ExtractOptions{});
        ran = true;
        break;
      case 4:
        analyze_samples(repo, *domain, make_sink);
        break;
      case 5:
        if (const std::optional<Domain> next = pick_domain()) domain = next;
        break;
      case 6:
        return;
    }
    if (ran) ui::wait_for_key("\n\033[90mpress any key to return to the menu\033[0m");
  }
}

}  // namespace sferic::cli
