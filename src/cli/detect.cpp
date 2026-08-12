#include "analysis/detect.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cli/commands.h"
#include "core/logger.h"
#include "io/audio_file.h"
#include "io/media_repository.h"

namespace sferic::cli {
namespace {

std::vector<io::FeatureFrameRow> feature_rows(const std::vector<analysis::FeatureFrame>& fs) {
  std::vector<io::FeatureFrameRow> rows;
  rows.reserve(fs.size());
  for (const analysis::FeatureFrame& f : fs)
    rows.push_back({f.t_s, f.low_band_db, f.centroid_hz, f.spread_hz, f.dominance, f.stereo_corr,
                    f.ms_width, f.phase_coherence, f.stereo_balance});
  return rows;
}

// Running tally of what carrying verdicts forward did across a corpus walk.
struct CarryTally {
  int approved = 0;   // windows that came back approved without review
  int rejected = 0;   // windows that came back rejected without review
  int pending = 0;    // windows nothing matched — these go to the review queue
  int unmatched = 0;  // reviewed windows this version no longer produces
};

// The verdict this detection inherits. Every window clearing `min_iou` is the
// same event by definition so the human's most recent ruling on it wins.
const io::ReviewedWindow* best_match(const analysis::TimeWindow& w,
                                     const std::vector<const io::ReviewedWindow*>& reviewed,
                                     const std::unordered_set<int64_t>& taken, double min_iou) {
  const io::ReviewedWindow* best = nullptr;
  double best_iou = 0.0;
  for (const io::ReviewedWindow* r : reviewed) {
    if (taken.contains(r->id)) continue;
    const double score = analysis::iou(w, r->start_s, r->end_s);
    if (score < min_iou) continue;
    if (best == nullptr || r->reviewed_at > best->reviewed_at ||
        (r->reviewed_at == best->reviewed_at && score > best_iou)) {
      best_iou = score;
      best = r;
    }
  }
  return best;
}

int insert_detections(io::MediaRepository& repo, int64_t run_id, int64_t media_id,
                      const analysis::IDetector& detector, const AudioBuffer& decoded,
                      const std::vector<const io::ReviewedWindow*>& reviewed, double min_iou,
                      std::vector<int64_t>& superseded, CarryTally& tally) {
  int per_media = 0;
  std::unordered_set<int64_t> taken;
  for (const analysis::Detection& d : detector.detect(decoded)) {
    io::ExemplarInsert row{
        run_id, media_id, d.window.start_s, d.window.end_s, d.window.confidence, std::nullopt};
    if (const io::ReviewedWindow* m = best_match(d.window, reviewed, taken, min_iou)) {
      row.carried = io::CarriedVerdict{m->approved, m->reject_reason, m->detector_version};
      taken.insert(m->id);
      superseded.push_back(m->id);
      (m->approved ? tally.approved : tally.rejected) += 1;
    } else {
      ++tally.pending;
    }
    const int64_t exemplar_id = repo.insert_exemplar(row);
    if (!d.features.empty()) repo.insert_exemplar_features(exemplar_id, feature_rows(d.features));
    ++per_media;
  }
  tally.unmatched += static_cast<int>(reviewed.size() - taken.size());
  return per_media;
}

template <typename MediaT>
int detect_corpus(io::MediaRepository& repo, Domain domain, CorpusRole role,
                  std::vector<MediaT> all, const analysis::IDetector& detector,
                  DetectOptions opts) {
  const std::string corpus = std::string(to_string(role));
  const std::string det_name{detector.name()};
  const std::string det_ver{detector.version()};

  if (opts.force) {  // forget stamps for selected media
    std::vector<int64_t> all_ids;
    all_ids.reserve(all.size());
    for (const MediaT& m : all) all_ids.push_back(m.id);
    repo.clear_detections_for_media(all_ids, det_name, det_ver);
  }

  const std::unordered_set<int64_t> already = repo.fetch_detected_media_ids(det_name, det_ver);

  std::vector<MediaT> media;
  media.reserve(all.size());
  for (MediaT& m : all) {
    if (already.contains(m.id)) continue;
    media.push_back(std::move(m));
  }
  if (media.empty()) {
    SFERIC_LOG(Info, "all " + std::to_string(all.size()) + " " + corpus + " media for domain=" +
                         std::string(to_string(domain)) + " already detected by " + det_name + "@" +
                         det_ver + " — nothing to do (pass --force to re-run)");
    return 0;
  }

  SFERIC_LOG(Info, "detecting " + std::to_string(media.size()) + " / " +
                       std::to_string(all.size()) + " " + corpus + " media (" +
                       std::to_string(all.size() - media.size()) + " skipped via ledger)");

  std::vector<int64_t> media_ids;
  media_ids.reserve(media.size());
  for (const MediaT& m : media) media_ids.push_back(m.id);
  std::vector<io::ReviewedWindow> reviewed;
  std::unordered_map<int64_t, std::vector<const io::ReviewedWindow*>> reviewed_by_media;
  if (opts.carry_verdicts) {
    reviewed = repo.fetch_reviewed_windows(domain);
    for (const io::ReviewedWindow& r : reviewed) reviewed_by_media[r.source_media_id].push_back(&r);
  }

  if (opts.force)
    repo.clear_exemplars_for_media(media_ids);
  else
    repo.clear_unreviewed_for_media(media_ids);

  const int64_t run_id = repo.begin_detector_run(det_name, det_ver, domain);
  int candidate_count = 0;
  CarryTally tally;
  std::vector<int64_t> superseded;
  const std::vector<const io::ReviewedWindow*> none_reviewed;
  for (size_t i = 0; i < media.size(); ++i) {
    const MediaT& m = media[i];
    // Keep the source channels — a detector mixes to mono for spectral passes
    // itself and may use L/R for stereo cues.
    const std::vector<std::byte> flac = repo.fetch_media_audio(m.id);
    const size_t audio_bytes = flac.size();
    const AudioBuffer decoded = io::decode(std::span<const std::byte>(flac));
    const auto it = reviewed_by_media.find(m.id);
    const int per_media =
        insert_detections(repo, run_id, m.id, detector, decoded,
                          it == reviewed_by_media.end() ? none_reviewed : it->second,
                          opts.carry_iou, superseded, tally);
    candidate_count += per_media;
    repo.mark_detected(m.id, det_name, det_ver, run_id, domain, per_media);

    SFERIC_PROGRESS(i + 1, media.size(), decoded.duration() * 1000.0, audio_bytes);
  }

  if (!opts.force) repo.delete_exemplars(superseded);
  repo.finish_detector_run(run_id, static_cast<int>(media.size()), candidate_count);
  SFERIC_LOG(Info, "detector_run " + std::to_string(run_id) + " produced " +
                       std::to_string(candidate_count) + " exemplar candidates from " +
                       std::to_string(media.size()) + " " + corpus + " media rows");
  if (opts.carry_verdicts)
    SFERIC_LOG(Info, "carried " + std::to_string(tally.approved) + " approved / " +
                         std::to_string(tally.rejected) + " rejected verdicts forward at iou>=" +
                         std::to_string(opts.carry_iou) + " — " + std::to_string(tally.pending) +
                         " new windows need review, " + std::to_string(tally.unmatched) +
                         " reviewed windows this version no longer produces (left in place)");
  return candidate_count;
}

}  // namespace

int walk_corpus(io::MediaRepository& repo, Domain domain, const analysis::IDetector& detector,
                DetectOptions opts) {
  SFERIC_SCOPE("walk_corpus");

  return opts.corpus == CorpusRole::Curated
             ? detect_corpus(repo, domain, opts.corpus, repo.fetch_curated(domain), detector, opts)
             : detect_corpus(repo, domain, opts.corpus, repo.fetch_reference(domain), detector,
                             opts);
}

}  // namespace sferic::cli
