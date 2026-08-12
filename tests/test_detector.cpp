#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <qr/qr.hpp>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis/detect.h"
#include "core/buffer.h"
#include "core/domain.h"
#include "io/audio_file.h"
#include "io/env.h"
#include "io/media_repository.h"
#include "module/thunder/detector.h"

using namespace sferic;
using namespace sferic::analysis;
using sferic::thunder::DetectorConfig;
using sferic::thunder::ThunderDetector;

struct Mark {
  double time;
  std::array<double, 3> tol_limit = {1, 2, 0.1};
  Mark(double t) : time(t) {}
  Mark(double t, std::array<double, 3> range) : time(t), tol_limit(range) {}
  std::optional<double> tol_fit(std::span<const double> detect) const {
    const auto [min, max, increment] = tol_limit;
    for (double t = min; t <= max; t += increment) {
      auto iter = std::ranges::find_if(detect, [&](double d) {
        return std::abs(d - time) <= t;
      });
      if (iter != detect.end()) return t;
    }
    return std::nullopt;
  }
};

TEST(Detector, generic) {
  io::env::load();
  qr::Connection db(io::env::database_url());
  io::MediaRepository repo(db);
  const ThunderDetector detector{DetectorConfig{}};
  for (const io::MarkedMedia& media : repo.fetch_marked_media(Domain::Thunder)) {
    const std::vector<std::byte> bytes = repo.fetch_media_audio(media.id);
    const AudioBuffer buffer = io::decode(std::span<const std::byte>(bytes));
    const std::vector<Detection> detections = detector.detect(buffer);
    const std::vector<double> onsets = detections | std::views::transform([](const Detection& d) {
                                         return d.window.start_s;
                                       }) |
                                       std::ranges::to<std::vector>();
    std::vector<Mark> marks;
    for (double t : media.marks) marks.emplace_back(t);

    EXPECT_EQ(onsets.size(), marks.size())
        << "freesound=" << media.external_id << " detected " << onsets.size()
        << " strikes, expected " << marks.size();
    for (const Mark& mark : marks) {
      const std::optional<double> matched_tol = mark.tol_fit(onsets);
      EXPECT_TRUE(matched_tol.has_value())
          << "freesound=" << media.external_id << " time=" << mark.time;
    }
  }
}

bool overlaps(const TimeWindow& w, double start_s, double end_s) {
  return w.start_s < end_s && start_s < w.end_s;
}

TEST(Detector, NoRegressionAgainstReviewedCorpus) {
  io::env::load();
  qr::Connection db(io::env::database_url());
  io::MediaRepository repo(db);
  const ThunderDetector detector{DetectorConfig{}};
  const std::vector<io::CuratedMedia> curated = repo.fetch_curated(Domain::Thunder);
  std::unordered_set<int64_t> curated_ids;
  for (const io::CuratedMedia& m : curated) curated_ids.insert(m.id);
  std::unordered_map<int64_t, std::vector<io::ExemplarCandidate>> approved_by_media;
  for (io::ExemplarCandidate& e : repo.fetch_approved(Domain::Thunder))
    if (curated_ids.contains(e.source_media_id))
      approved_by_media[e.source_media_id].push_back(std::move(e));
  std::unordered_map<int64_t, std::vector<io::ExemplarCandidate>> rejected_by_media;
  for (io::ExemplarCandidate& e : repo.fetch_rejected(Domain::Thunder))
    if (curated_ids.contains(e.source_media_id))
      rejected_by_media[e.source_media_id].push_back(std::move(e));
  size_t checked_media = 0;
  size_t checked_approved = 0;
  size_t rejected_total = 0;
  size_t rejected_still_emitted = 0;
  for (const io::CuratedMedia& media : curated) {
    const auto approved_it = approved_by_media.find(media.id);
    if (approved_it == approved_by_media.end()) continue;
    ++checked_media;
    const std::vector<std::byte> flac = repo.fetch_media_audio(media.id);
    const AudioBuffer buffer = io::decode(std::span<const std::byte>(flac));
    const std::vector<Detection> detections = detector.detect(buffer);
    for (const io::ExemplarCandidate& ex : approved_it->second) {
      ++checked_approved;
      const bool matched = std::ranges::any_of(detections, [&](const Detection& d) {
        return overlaps(d.window, ex.start_s, ex.end_s);
      });
      EXPECT_TRUE(matched) << "media=" << media.id << " lost a previously-approved strike ["
                           << ex.start_s << "s, " << ex.end_s << "s]";
    }
    const auto rejected_it = rejected_by_media.find(media.id);
    if (rejected_it == rejected_by_media.end()) continue;
    for (const io::ExemplarCandidate& ex : rejected_it->second) {
      ++rejected_total;
      const bool still_emitted = std::ranges::any_of(detections, [&](const Detection& d) {
        return overlaps(d.window, ex.start_s, ex.end_s);
      });
      rejected_still_emitted += still_emitted;
    }
  }

  EXPECT_GT(checked_media, 0u) << "no curated media with a reviewed exemplar — "
                                  "run `sferic detect thunder --curated` then "
                                  "`sferic review thunder` before this test has anything to check";
  std::cout << "[reviewed-corpus regression] " << checked_media << " media, " << checked_approved
            << " previously-approved strikes still found; " << rejected_still_emitted << "/"
            << rejected_total
            << " previously-rejected windows still emitted (needs re-review, not a failure)\n";
}
