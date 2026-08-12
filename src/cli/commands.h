#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "analysis/detect.h"
#include "core/buffer.h"
#include "core/domain.h"
#include "io/media_repository.h"
#include "ui/player.h"

namespace sferic::cli {

struct DetectOptions {
  // Re-process media already stamped for this detector name@version. Clears the
  // ledger for exactly the media in `corpus`, then runs normally. Every exemplar
  // row for those media is dropped first, approved and rejected ones included,
  // so fresh windows cannot land beside stale duplicates — but the verdicts
  // themselves survive when `carry_verdicts` is set, because they are read into
  // memory before the clear. Pass --no-carry for a re-judge from scratch.
  bool force = false;
  // Which population to walk. `reference` validates the segmenter in bulk
  CorpusRole corpus = CorpusRole::Reference;
  // Carry a human verdict onto a new window that matches one already ruled on
  // instead of queueing it for review again.
  bool carry_verdicts = true;
  // How much a new window must overlap a reviewed one to count as the same
  // event, as intersection over union.
  double carry_iou = 0.6;
};

struct ExtractOptions {
  bool force = false;  // rebuild every approved window instead of resuming
};

struct AnalyzeOptions {
  bool playback = false;  // TODO
  bool plot = false;      // TODO
};

enum class ReviewSet : uint8_t { Pending, Approved, Rejected };

constexpr std::string_view to_string(ReviewSet s) {
  switch (s) {
    case ReviewSet::Pending:
      return "pending";
    case ReviewSet::Approved:
      return "approved";
    case ReviewSet::Rejected:
      return "rejected";
  }
  std::unreachable();
}

inline ReviewSet from_string(std::string_view s, std::type_identity<ReviewSet>) {
  if (s == "pending") return ReviewSet::Pending;
  if (s == "approved") return ReviewSet::Approved;
  if (s == "rejected") return ReviewSet::Rejected;
  throw std::invalid_argument("unknown ReviewSet: " + std::string(s));
}

int ingest(Domain domain);
int walk_corpus(io::MediaRepository& repo, Domain domain, const analysis::IDetector& detector,
                DetectOptions opts = {});
void extract(io::MediaRepository& repo, Domain domain, ExtractOptions opts = {});
double analyze_sample(const AudioBuffer& sample, const std::string& label);
void analyze(io::MediaRepository& repo, Domain domain, const AnalyzeOptions& opts);
void review(io::MediaRepository& repo, Domain domain, ReviewSet set,
            const ui::AudioSinkFactory& make_sink);
void library(io::MediaRepository& repo, Domain domain, const ui::AudioSinkFactory& make_sink);
void analyze_samples(io::MediaRepository& repo, Domain domain,
                     const ui::AudioSinkFactory& make_sink);

}  // namespace sferic::cli
