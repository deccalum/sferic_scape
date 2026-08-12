#include "io/media_repository.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <qr/qr.hpp>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/logger.h"
#include "io/schema.h"

namespace sferic::io {

using namespace schema;

MediaRepository::MediaRepository(qr::Connection& db) : db_(db) {
  SFERIC_LOG(Info, "repository bound to Postgres: " + std::string(db_.dbname()));
}

std::vector<ReferenceMedia> MediaRepository::fetch_reference(Domain domain) const {
  SFERIC_SCOPE("fetch_reference");

  auto query = qr::select(Media::id, Media::sample_rate, Media::channels, Media::codec)
                   .distinct()
                   .from<Media>()
                   .join<MediaLabels>(qr::on(MediaLabels::media_id, Media::id))
                   .join<Labels>(qr::on(Labels::id, MediaLabels::label_id))
                   .where(Media::corpus_role == CorpusRole::Reference &&
                          Labels::domain == to_string(domain));

  std::vector<ReferenceMedia> out = db_.select_as<ReferenceMedia>(query);

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " reference media rows for domain=" + std::string(to_string(domain)));
  return out;
}

std::vector<CuratedMedia> MediaRepository::fetch_curated(Domain domain) const {
  SFERIC_SCOPE("fetch_curated");

  auto query = qr::select(Media::id, Media::sample_rate, Media::channels, Media::codec)
                   .distinct()
                   .from<Media>()
                   .join<MediaLabels>(qr::on(MediaLabels::media_id, Media::id))
                   .join<Labels>(qr::on(Labels::id, MediaLabels::label_id))
                   .where(Media::corpus_role == CorpusRole::Curated &&
                          Labels::domain == to_string(domain));

  std::vector<CuratedMedia> out = db_.select_as<CuratedMedia>(query);

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " curated media rows for domain=" + std::string(to_string(domain)));
  return out;
}

std::vector<MarkedMedia> MediaRepository::fetch_marked_media(Domain domain) const {
  SFERIC_SCOPE("fetch_marked_media");

  auto query =
      qr::select(Media::id, Media::external_id, Media::marks)
          .distinct()
          .from<Media>()
          .join<MediaLabels>(qr::on(MediaLabels::media_id, Media::id))
          .join<Labels>(qr::on(Labels::id, MediaLabels::label_id))
          .where(Media::corpus_role == CorpusRole::Curated && Labels::domain == to_string(domain) &&
                 qr::is_not_null(Media::marks));

  std::vector<MarkedMedia> out = db_.select_rows(
      query, [](int64_t id, std::string external_id, std::optional<std::vector<double>> marks) {
        return MarkedMedia{id, std::move(external_id), std::move(*marks)};
      });

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " marked media rows for domain=" + std::string(to_string(domain)));
  return out;
}

int64_t MediaRepository::begin_detector_run(const std::string& detector_name,
                                            const std::string& detector_version,
                                            Domain domain) const {
  auto query = qr::insert_into<DetectorRun>()
                   .set(DetectorRun::detector_name, detector_name)
                   .set(DetectorRun::detector_version, detector_version)
                   .set(DetectorRun::domain, domain)
                   .set(DetectorRun::media_count, 0)
                   .set(DetectorRun::candidate_count, 0)
                   .returning(DetectorRun::id);

  const int64_t id = db_.insert_returning<int64_t>(query);

  SFERIC_LOG(Info, "began detector_run " + std::to_string(id) + " (" + detector_name + " " +
                       detector_version + ")");
  return id;
}

std::size_t MediaRepository::clear_unreviewed(Domain domain) const {
  SFERIC_SCOPE("clear_unreviewed");

  auto run_query =
      qr::select(DetectorRun::id).from<DetectorRun>().where(DetectorRun::domain == domain);
  const std::vector<int64_t> run_ids = db_.select_rows(run_query);

  auto del = qr::delete_from<Exemplar>().where(qr::in(Exemplar::detector_run_id, run_ids) &&
                                               qr::is_null(Exemplar::approved));
  const std::size_t removed = db_.remove(del);

  SFERIC_LOG(Info, "cleared " + std::to_string(removed) +
                       " unreviewed exemplars for domain=" + std::string(to_string(domain)) +
                       " across " + std::to_string(run_ids.size()) + " detector runs");
  return removed;
}

std::size_t MediaRepository::clear_unreviewed_for_media(
    const std::vector<int64_t>& media_ids) const {
  SFERIC_SCOPE("clear_unreviewed_for_media");

  auto del = qr::delete_from<Exemplar>().where(qr::in(Exemplar::source_media_id, media_ids) &&
                                               qr::is_null(Exemplar::approved));
  const std::size_t removed = db_.remove(del);

  SFERIC_LOG(Info, "cleared " + std::to_string(removed) + " unreviewed exemplars across " +
                       std::to_string(media_ids.size()) + " media rows");
  return removed;
}

std::size_t MediaRepository::clear_exemplars_for_media(
    const std::vector<int64_t>& media_ids) const {
  SFERIC_SCOPE("clear_exemplars_for_media");

  auto reviewed_q = qr::select(Exemplar::id)
                        .from<Exemplar>()
                        .where(qr::in(Exemplar::source_media_id, media_ids) &&
                               qr::is_not_null(Exemplar::approved));
  const std::size_t reviewed = db_.count(reviewed_q);

  auto del = qr::delete_from<Exemplar>().where(qr::in(Exemplar::source_media_id, media_ids));
  const std::size_t removed = db_.remove(del);

  SFERIC_LOG(Warn, "cleared " + std::to_string(removed) + " exemplars across " +
                       std::to_string(media_ids.size()) + " media rows — " +
                       std::to_string(reviewed) + " of them already reviewed");
  return removed;
}

std::unordered_set<int64_t> MediaRepository::fetch_detected_media_ids(
    const std::string& detector_name, const std::string& detector_version) const {
  SFERIC_SCOPE("fetch_detected_media_ids");

  auto query = qr::select(MediaDetection::media_id)
                   .from<MediaDetection>()
                   .where(MediaDetection::detector_name == detector_name &&
                          MediaDetection::detector_version == detector_version);

  const std::vector<int64_t> ids = db_.select_rows(query);
  std::unordered_set<int64_t> out(ids.begin(), ids.end());

  SFERIC_LOG(Info, "ledger has " + std::to_string(out.size()) + " media for " + detector_name +
                       "@" + detector_version);
  return out;
}

void MediaRepository::mark_detected(int64_t media_id, const std::string& detector_name,
                                    const std::string& detector_version, int64_t detector_run_id,
                                    Domain domain, int candidate_count) const {
  auto query = qr::insert_into<MediaDetection>()
                   .set(MediaDetection::media_id, media_id)
                   .set(MediaDetection::detector_name, detector_name)
                   .set(MediaDetection::detector_version, detector_version)
                   .set(MediaDetection::detector_run_id, detector_run_id)
                   .set(MediaDetection::domain, domain)
                   .set(MediaDetection::candidate_count, candidate_count);

  db_.insert(query);
}

std::size_t MediaRepository::clear_detections_for_media(const std::vector<int64_t>& media_ids,
                                                        const std::string& detector_name,
                                                        const std::string& detector_version) const {
  SFERIC_SCOPE("clear_detections_for_media");

  auto del = qr::delete_from<MediaDetection>().where(
      qr::in(MediaDetection::media_id, media_ids) &&
      MediaDetection::detector_name == detector_name &&
      MediaDetection::detector_version == detector_version);
  const std::size_t removed = db_.remove(del);

  SFERIC_LOG(Info, "cleared " + std::to_string(removed) + " media_detection rows across " +
                       std::to_string(media_ids.size()) + " media for " + detector_name + "@" +
                       detector_version);
  return removed;
}

int64_t MediaRepository::insert_exemplar(const ExemplarInsert& e) const {
  auto query = qr::insert_into<Exemplar>()
                   .set(Exemplar::source_media_id, e.source_media_id)
                   .set(Exemplar::detector_run_id, e.detector_run_id)
                   .set(Exemplar::start_s, e.start_s)
                   .set(Exemplar::end_s, e.end_s)
                   .set(Exemplar::confidence, e.confidence)
                   .set(Exemplar::approved, e.carried ? std::optional<bool>(e.carried->approved)
                                                      : std::nullopt)
                   .set(Exemplar::reject_reason, e.carried ? e.carried->reject_reason
                                                           : std::nullopt)
                   .set(Exemplar::carried_from,
                        e.carried ? std::optional<std::string>(e.carried->from_version)
                                  : std::nullopt)
                   .returning(Exemplar::id);

  return db_.insert_returning<int64_t>(query);
}

std::vector<ReviewedWindow> MediaRepository::fetch_reviewed_windows(Domain domain) const {
  SFERIC_SCOPE("fetch_reviewed_windows");

  auto query = qr::select(Exemplar::id, Exemplar::source_media_id, Exemplar::start_s,
                          Exemplar::end_s, Exemplar::approved, Exemplar::reject_reason,
                          DetectorRun::detector_version, Exemplar::reviewed_at)
                   .from<Exemplar>()
                   .join<DetectorRun>(qr::on(DetectorRun::id, Exemplar::detector_run_id))
                   .where(DetectorRun::domain == domain && qr::is_not_null(Exemplar::approved))
                   .order_by(Exemplar::source_media_id);

  std::vector<ReviewedWindow> out = db_.select_rows(
      query, [](int64_t id, int64_t source_media_id, double start_s, double end_s,
                std::optional<bool> approved, std::optional<RejectReason> reject_reason,
                std::string detector_version,
                std::optional<std::chrono::system_clock::time_point> reviewed_at) {
        return ReviewedWindow{id,        source_media_id,             start_s,
                              end_s,     *approved,                   reject_reason,
                              std::move(detector_version), reviewed_at};
      });

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " reviewed windows for domain=" + std::string(to_string(domain)));
  return out;
}

std::size_t MediaRepository::delete_exemplars(const std::vector<int64_t>& exemplar_ids) const {
  if (exemplar_ids.empty()) return 0;
  SFERIC_SCOPE("delete_exemplars");

  auto del = qr::delete_from<Exemplar>().where(qr::in(Exemplar::id, exemplar_ids));
  const std::size_t removed = db_.remove(del);

  SFERIC_LOG(Info, "deleted " + std::to_string(removed) + " superseded exemplars");
  return removed;
}

void MediaRepository::insert_exemplar_features(int64_t exemplar_id,
                                               const std::vector<FeatureFrameRow>& frames) const {
  auto query = qr::bulk_insert_into<ExemplarFeature>(
      ExemplarFeature::exemplar_id, ExemplarFeature::frame_index, ExemplarFeature::t_s,
      ExemplarFeature::low_band_db, ExemplarFeature::centroid_hz, ExemplarFeature::spread_hz,
      ExemplarFeature::dominance, ExemplarFeature::stereo_corr, ExemplarFeature::ms_width,
      ExemplarFeature::phase_coherence, ExemplarFeature::stereo_balance);

  for (size_t i = 0; i < frames.size(); ++i) {
    const FeatureFrameRow& f = frames[i];
    query.add(exemplar_id, static_cast<int>(i), f.t_s, f.low_band_db, f.centroid_hz, f.spread_hz,
              f.dominance, f.stereo_corr, f.ms_width, f.phase_coherence, f.stereo_balance);
  }

  db_.insert_bulk(query);
}

void MediaRepository::finish_detector_run(int64_t run_id, int media_count,
                                          int candidate_count) const {
  auto query = qr::update<DetectorRun>()
                   .set(DetectorRun::finished_at, qr::now())
                   .set(DetectorRun::media_count, media_count)
                   .set(DetectorRun::candidate_count, candidate_count)
                   .where(DetectorRun::id == run_id);

  db_.update(query);

  SFERIC_LOG(Info, "finished detector_run " + std::to_string(run_id) + ": " +
                       std::to_string(media_count) + " media, " + std::to_string(candidate_count) +
                       " candidates");
}

std::vector<ExemplarCandidate> MediaRepository::fetch_unreviewed(Domain domain) const {
  SFERIC_SCOPE("fetch_unreviewed");

  auto query = qr::select(Exemplar::id, Exemplar::source_media_id, Exemplar::start_s,
                          Exemplar::end_s, Exemplar::confidence)
                   .from<Exemplar>()
                   .join<DetectorRun>(qr::on(DetectorRun::id, Exemplar::detector_run_id))
                   .where(DetectorRun::domain == domain && qr::is_null(Exemplar::approved))
                   .order_by(Exemplar::source_media_id);

  std::vector<ExemplarCandidate> out =
      db_.select_rows(query, [](int64_t id, int64_t source_media_id, double start_s, double end_s,
                                std::optional<double> confidence) {
        return ExemplarCandidate{id,    source_media_id,          start_s,
                                 end_s, confidence.value_or(0.0), std::nullopt};
      });

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " unreviewed exemplars for domain=" + std::string(to_string(domain)));
  return out;
}

std::vector<std::byte> MediaRepository::fetch_media_audio(int64_t media_id) const {
  SFERIC_SCOPE("fetch_media_audio");

  auto query = qr::select(Media::audio).from<Media>().where(Media::id == media_id);
  std::vector<std::vector<std::byte>> rows = db_.select_rows(query);

  SFERIC_LOG(Info, "fetched media " + std::to_string(media_id) + " audio " +
                       std::to_string(rows.front().size()) + " bytes");
  return std::move(rows.front());
}

void MediaRepository::approve_exemplar(int64_t exemplar_id) const {
  auto query = qr::update<Exemplar>()
                   .set(Exemplar::approved, true)
                   .set(Exemplar::reject_reason, std::nullopt)
                   .set(Exemplar::reviewed_at, qr::now())
                   .where(Exemplar::id == exemplar_id);

  db_.update(query);

  SFERIC_LOG(Info, "approved exemplar " + std::to_string(exemplar_id));
}

void MediaRepository::reject_exemplar(int64_t exemplar_id, RejectReason reason) const {
  auto query = qr::update<Exemplar>()
                   .set(Exemplar::approved, false)
                   .set(Exemplar::reject_reason, reason)
                   .set(Exemplar::reviewed_at, qr::now())
                   .where(Exemplar::id == exemplar_id);

  db_.update(query);

  SFERIC_LOG(Info, "rejected exemplar " + std::to_string(exemplar_id) + ": " +
                       std::string(to_string(reason)));
}

std::vector<ExemplarCandidate> MediaRepository::fetch_approved(Domain domain) const {
  SFERIC_SCOPE("fetch_approved");

  auto query = qr::select(Exemplar::id, Exemplar::source_media_id, Exemplar::start_s,
                          Exemplar::end_s, Exemplar::confidence)
                   .from<Exemplar>()
                   .join<DetectorRun>(qr::on(DetectorRun::id, Exemplar::detector_run_id))
                   .where(DetectorRun::domain == domain && Exemplar::approved == true)
                   .order_by(Exemplar::source_media_id);

  std::vector<ExemplarCandidate> out =
      db_.select_rows(query, [](int64_t id, int64_t source_media_id, double start_s, double end_s,
                                std::optional<double> confidence) {
        return ExemplarCandidate{id,    source_media_id,          start_s,
                                 end_s, confidence.value_or(0.0), std::nullopt};
      });

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " approved exemplars for domain=" + std::string(to_string(domain)));
  return out;
}

std::vector<ExemplarCandidate> MediaRepository::fetch_rejected(Domain domain) const {
  SFERIC_SCOPE("fetch_rejected");

  auto query = qr::select(Exemplar::id, Exemplar::source_media_id, Exemplar::start_s,
                          Exemplar::end_s, Exemplar::confidence, Exemplar::reject_reason)
                   .from<Exemplar>()
                   .join<DetectorRun>(qr::on(DetectorRun::id, Exemplar::detector_run_id))
                   .where(DetectorRun::domain == domain && Exemplar::approved == false)
                   .order_by(Exemplar::source_media_id);

  std::vector<ExemplarCandidate> out = db_.select_rows(
      query, [](int64_t id, int64_t source_media_id, double start_s, double end_s,
                std::optional<double> confidence, std::optional<RejectReason> reject_reason) {
        return ExemplarCandidate{id,    source_media_id,          start_s,
                                 end_s, confidence.value_or(0.0), reject_reason};
      });

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) +
                       " rejected exemplars for domain=" + std::string(to_string(domain)));
  return out;
}

std::vector<MediaSourceInfo> MediaRepository::fetch_media_catalog() const {
  SFERIC_SCOPE("fetch_media_catalog");

  auto media_q = qr::select(Media::id, Media::origin, Media::corpus_role, Media::external_id,
                            Media::start_s, Media::end_s)
                     .from<Media>();

  std::vector<MediaSourceInfo> rows = db_.select_rows(
      media_q, [](int64_t id, std::string origin, CorpusRole role, std::string external_id,
                  std::optional<double> start_s, std::optional<double> end_s) {
        MediaSourceInfo r;
        r.id = id;
        r.origin = std::move(origin);
        r.corpus_role = role;
        r.external_id = std::move(external_id);
        r.start_s = start_s;
        r.end_s = end_s;
        if (start_s && end_s && *end_s > *start_s) r.length_s = *end_s - *start_s;
        r.group = r.origin + std::string(1, '\0') + r.external_id;
        r.url = r.origin + ":" + r.external_id;
        return r;
      });

  auto yt_q = qr::select(YtVideo::media_id, YtVideo::ytid, YtVideo::title, YtVideo::webpage_url)
                  .from<YtVideo>();
  struct YtMeta {
    std::string ytid;
    std::optional<std::string> title;
    std::optional<std::string> webpage_url;
  };
  std::unordered_map<int64_t, YtMeta> yt_by_media;
  {
    auto yt_rows = db_.select_rows(
        yt_q, [](int64_t media_id, std::string ytid, std::optional<std::string> title,
                 std::optional<std::string> webpage_url) {
          return std::make_pair(media_id,
                                YtMeta{std::move(ytid), std::move(title), std::move(webpage_url)});
        });
    for (auto& [id, meta] : yt_rows) yt_by_media.emplace(id, std::move(meta));
  }

  for (MediaSourceInfo& r : rows) {
    if (auto it = yt_by_media.find(r.id); it != yt_by_media.end()) {
      const YtMeta& y = it->second;
      if (y.webpage_url && !y.webpage_url->empty())
        r.url = *y.webpage_url;
      else if (!y.ytid.empty())
        r.url = "https://www.youtube.com/watch?v=" + y.ytid;
      if (y.title) r.title = *y.title;
      r.group = "youtube" + std::string(1, '\0') + y.ytid;
    }
  }

  auto fs_q =
      qr::select(FreeSound::media_id, FreeSound::title, FreeSound::username, FreeSound::webpage_url)
          .from<FreeSound>();
  struct FsMeta {
    std::optional<std::string> title;
    std::optional<std::string> username;
    std::optional<std::string> webpage_url;
  };
  std::unordered_map<int64_t, FsMeta> fs_by_media;
  {
    auto fs_rows = db_.select_rows(
        fs_q, [](int64_t media_id, std::optional<std::string> title,
                 std::optional<std::string> username, std::optional<std::string> webpage_url) {
          return std::make_pair(
              media_id, FsMeta{std::move(title), std::move(username), std::move(webpage_url)});
        });
    for (auto& [id, meta] : fs_rows) fs_by_media.emplace(id, std::move(meta));
  }
  for (MediaSourceInfo& r : rows) {
    if (auto it = fs_by_media.find(r.id); it != fs_by_media.end()) {
      const FsMeta& f = it->second;
      if (f.webpage_url && !f.webpage_url->empty()) r.url = *f.webpage_url;
      if (f.title && !f.title->empty()) r.title = *f.title;
      if (f.username && !f.username->empty()) r.recorded_by = *f.username;
    }
  }

  std::sort(rows.begin(), rows.end(), [](const MediaSourceInfo& a, const MediaSourceInfo& b) {
    if (a.group != b.group) return a.group < b.group;
    const double as = a.start_s.value_or(-1.0);
    const double bs = b.start_s.value_or(-1.0);
    if (as != bs) return as < bs;
    return a.id < b.id;
  });

  for (size_t i = 0; i < rows.size();) {
    size_t j = i + 1;
    while (j < rows.size() && rows[j].group == rows[i].group) ++j;
    const int n = static_cast<int>(j - i);
    for (size_t k = i; k < j; ++k) {
      rows[k].part = static_cast<int>(k - i) + 1;
      rows[k].parts = n;
    }
    i = j;
  }

  SFERIC_LOG(Info, "media catalog " + std::to_string(rows.size()) + " rows");
  return rows;
}

std::optional<int64_t> MediaRepository::fetch_media_id(const std::string& origin,
                                                        const std::string& external_id) const {
  SFERIC_SCOPE("fetch_media_id");

  auto query = qr::select(Media::id)
                   .from<Media>()
                   .where(Media::origin == origin && Media::external_id == external_id)
                   .limit(1);
  const std::vector<int64_t> rows = db_.select_rows(query);

  SFERIC_LOG(Info, "fetch_media_id " + origin + ":" + external_id + " -> " +
                       (rows.empty() ? "not found" : std::to_string(rows.front())));
  return rows.empty() ? std::nullopt : std::optional(rows.front());
}

LibraryCounts MediaRepository::count_library(Domain domain) const {
  SFERIC_SCOPE("count_library");
  const std::string dom = std::string(to_string(domain));

  const auto media_count = [&](CorpusRole role) {
    auto q = qr::select(Media::id)
                 .distinct()
                 .from<Media>()
                 .join<MediaLabels>(qr::on(MediaLabels::media_id, Media::id))
                 .join<Labels>(qr::on(Labels::id, MediaLabels::label_id))
                 .where(Media::corpus_role == role && Labels::domain == dom);
    return static_cast<int>(db_.count(q));
  };

  const auto exemplar_count = [&](qr::Predicate pred) {
    auto q = qr::select(Exemplar::id)
                 .from<Exemplar>()
                 .join<DetectorRun>(qr::on(DetectorRun::id, Exemplar::detector_run_id))
                 .where(std::move(pred));
    return static_cast<int>(db_.count(q));
  };

  auto samples_q = qr::select(Processed::id).from<Processed>().where(Processed::domain == domain);
  const int samples = static_cast<int>(db_.count(samples_q));

  const LibraryCounts counts{
      media_count(CorpusRole::Reference),
      media_count(CorpusRole::Curated),
      exemplar_count(DetectorRun::domain == domain && qr::is_null(Exemplar::approved)),
      exemplar_count(DetectorRun::domain == domain && Exemplar::approved == true),
      exemplar_count(DetectorRun::domain == domain && Exemplar::approved == false),
      samples,
  };
  SFERIC_LOG(Info, "library " + dom + ": media " + std::to_string(counts.reference_media) + "/" +
                       std::to_string(counts.curated_media) + " (ref/cur), exemplar " +
                       std::to_string(counts.pending) + "/" + std::to_string(counts.approved) + "/" +
                       std::to_string(counts.rejected) + " (pend/appr/rej), samples " +
                       std::to_string(counts.samples));
  return counts;
}

std::vector<MediaSourceInfo> MediaRepository::fetch_recordings(
    Domain domain, std::optional<CorpusRole> role) const {
  SFERIC_SCOPE("fetch_recordings");

  auto id_q = qr::select(Media::id)
                  .distinct()
                  .from<Media>()
                  .join<MediaLabels>(qr::on(MediaLabels::media_id, Media::id))
                  .join<Labels>(qr::on(Labels::id, MediaLabels::label_id))
                  .where(Labels::domain == std::string(to_string(domain)));
  const std::vector<int64_t> id_vec = db_.select_rows(id_q);
  const std::unordered_set<int64_t> ids(id_vec.begin(), id_vec.end());

  std::vector<MediaSourceInfo> catalog = fetch_media_catalog();
  std::vector<MediaSourceInfo> out;
  for (MediaSourceInfo& m : catalog) {
    if (!ids.contains(m.id)) continue;
    if (role && m.corpus_role != *role) continue;
    out.push_back(std::move(m));
  }

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) + " recordings for domain=" +
                       std::string(to_string(domain)) +
                       (role ? " role=" + std::string(to_string(*role)) : " (all corpora)"));
  return out;
}

std::vector<ProcessedRow> MediaRepository::fetch_processed(Domain domain,
                                                           std::optional<CorpusRole> role) const {
  SFERIC_SCOPE("fetch_processed");

  auto query =
      qr::select(Processed::id, Processed::source_media_id, Processed::source_external_id,
                 Processed::source_title, Processed::source_recorded_by, Processed::source_url,
                 Processed::corpus_role, Processed::duration_s, Processed::confidence,
                 Processed::size_bytes, Processed::noise_floor, Processed::peak, Processed::start_s,
                 Processed::end_s)
          .from<Processed>();
  if (role)
    query.where(Processed::domain == domain && Processed::corpus_role == *role);
  else
    query.where(Processed::domain == domain);
  query.order_by(Processed::id);

  std::vector<ProcessedRow> out = db_.select_as<ProcessedRow>(query);

  SFERIC_LOG(Info, "fetched " + std::to_string(out.size()) + " processed samples for domain=" +
                       std::string(to_string(domain)) +
                       (role ? " role=" + std::string(to_string(*role)) : " (all corpora)"));
  return out;
}

std::vector<std::byte> MediaRepository::fetch_processed_audio(int64_t processed_id) const {
  SFERIC_SCOPE("fetch_processed_audio");

  auto query = qr::select(Processed::audio).from<Processed>().where(Processed::id == processed_id);
  std::vector<std::vector<std::byte>> rows = db_.select_rows(query);

  SFERIC_LOG(Info, "fetched processed " + std::to_string(processed_id) + " audio " +
                       std::to_string(rows.front().size()) + " bytes");
  return std::move(rows.front());
}

std::set<std::tuple<int64_t, int64_t, int64_t>> MediaRepository::fetch_processed_windows(
    Domain domain) const {
  SFERIC_SCOPE("fetch_processed_windows");

  auto query = qr::select(Processed::source_media_id, Processed::start_s, Processed::end_s)
                   .from<Processed>()
                   .where(Processed::domain == domain);
  std::vector<std::tuple<int64_t, int64_t, int64_t>> rows =
      db_.select_rows(query, [](int64_t media_id, double start_s, double end_s) {
        return std::tuple<int64_t, int64_t, int64_t>{
            media_id, static_cast<int64_t>(std::llround(start_s * 1000.0)),
            static_cast<int64_t>(std::llround(end_s * 1000.0))};
      });

  std::set<std::tuple<int64_t, int64_t, int64_t>> out(rows.begin(), rows.end());
  SFERIC_LOG(Info, "found " + std::to_string(out.size()) + " already-extracted windows for domain=" +
                       std::string(to_string(domain)));
  return out;
}

std::vector<BandRow> MediaRepository::fetch_sample_bands(int64_t processed_id) const {
  auto query = qr::select(SampleBand::band_index, SampleBand::amount, SampleBand::f_lo_hz,
                          SampleBand::f_hi_hz)
                   .from<SampleBand>()
                   .where(SampleBand::processed_id == processed_id)
                   .order_by(SampleBand::band_index);
  return db_.select_as<BandRow>(query);
}

std::optional<AdsrRow> MediaRepository::fetch_sample_adsr(int64_t processed_id) const {
  auto query = qr::select(Adsr::attack_s, Adsr::peak_s, Adsr::release_s, Adsr::sustain_level)
                   .from<Adsr>()
                   .where(Adsr::processed_id == processed_id);
  std::vector<AdsrRow> rows = db_.select_as<AdsrRow>(query);
  if (rows.empty()) return std::nullopt;
  return rows.front();
}

std::size_t MediaRepository::clear_processed(Domain domain) const {
  SFERIC_SCOPE("clear_processed");

  auto del = qr::delete_from<Processed>().where(Processed::domain == domain);
  const std::size_t removed = db_.remove(del);  // CASCADE drops adsr / sample_band children

  SFERIC_LOG(Info, "cleared " + std::to_string(removed) + " processed rows for domain=" +
                       std::string(to_string(domain)));
  return removed;
}

int64_t MediaRepository::insert_processed(const ProcessedInsert& p) const {
  auto query = qr::insert_into<Processed>()
                   .set(Processed::corpus_role, p.corpus_role)
                   .set(Processed::source_media_id, p.source_media_id)
                   .set(Processed::source_origin, p.source_origin)
                   .set(Processed::source_external_id, p.source_external_id)
                   .set(Processed::source_title, p.source_title)
                   .set(Processed::source_recorded_by, p.source_recorded_by)
                   .set(Processed::source_url, p.source_url)
                   .set(Processed::domain, p.domain)
                   .set(Processed::duration_s, p.duration_s)
                   .set(Processed::size_bytes, p.size_bytes)
                   .set(Processed::start_s, p.start_s)
                   .set(Processed::end_s, p.end_s)
                   .set(Processed::start_frame, p.start_frame)
                   .set(Processed::end_frame, p.end_frame)
                   .set(Processed::confidence, p.confidence)
                   .set(Processed::audio, p.audio)
                   .set(Processed::codec, p.codec)
                   .set(Processed::noise_floor, p.noise_floor)
                   .set(Processed::peak, p.peak)
                   .set(Processed::stereo_invariance, p.stereo_invariance)
                   .set(Processed::children, p.children)
                   .returning(Processed::id);

  const int64_t id = db_.insert_returning<int64_t>(std::move(query));
  SFERIC_LOG(Info, "processed " + std::to_string(id) + " from media " +
                       std::to_string(p.source_media_id) + " (" + std::to_string(p.audio.size()) +
                       " FLAC bytes)");
  return id;
}

void MediaRepository::insert_adsr(int64_t processed_id, double attack_s, double peak_s,
                                  double release_s, double sustain_level) const {
  auto query = qr::insert_into<Adsr>()
                   .set(Adsr::processed_id, processed_id)
                   .set(Adsr::attack_s, attack_s)
                   .set(Adsr::peak_s, peak_s)
                   .set(Adsr::release_s, release_s)
                   .set(Adsr::sustain_level, sustain_level);
  db_.insert(query);
}

void MediaRepository::insert_sample_band(int64_t processed_id, int band_index, double amount,
                                         double f_lo_hz, double f_hi_hz) const {
  auto query = qr::insert_into<SampleBand>()
                   .set(SampleBand::processed_id, processed_id)
                   .set(SampleBand::band_index, band_index)
                   .set(SampleBand::amount, amount)
                   .set(SampleBand::f_lo_hz, f_lo_hz)
                   .set(SampleBand::f_hi_hz, f_hi_hz);
  db_.insert(query);
}

void MediaRepository::store_resynth_score(int64_t processed_id, double similarity_pct) const {
  auto query = qr::insert_into<ParametricModel>()
                   .set(ParametricModel::processed_id, processed_id)
                   .set(ParametricModel::resynth_similarity, similarity_pct)
                   .on_conflict(ParametricModel::processed_id)
                   .do_update(ParametricModel::resynth_similarity)
                   .do_update(ParametricModel::analyzed_at, qr::now());
  db_.insert(std::move(query));

  SFERIC_LOG(Info, "stored resynth similarity " + std::to_string(similarity_pct) +
                       "% for processed " + std::to_string(processed_id));
}

std::optional<double> MediaRepository::fetch_resynth_score(int64_t processed_id) const {
  auto query = qr::select(ParametricModel::resynth_similarity)
                   .from<ParametricModel>()
                   .where(ParametricModel::processed_id == processed_id);
  std::vector<double> rows = db_.select_rows(query);
  if (rows.empty()) return std::nullopt;
  return rows.front();
}

}  // namespace sferic::io
