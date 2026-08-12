#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "core/domain.h"

namespace qr {
class Connection;
}

namespace sferic::io {

struct ReferenceMedia {
  int64_t id;         // media.id
  int sample_rate;    // media.sample_rate, Hz
  int channels;       // media.channels
  std::string codec;  // media.codec (e.g. "flac")
};

struct CuratedMedia {
  int64_t id;         // media.id
  int sample_rate;    // media.sample_rate, Hz
  int channels;       // media.channels
  std::string codec;  // media.codec (e.g. "flac")
};

struct MarkedMedia {
  int64_t id;                 // media.id
  std::string external_id;    // e.g. freesound id, for the log/report line
  std::vector<double> marks;  // expected event onset seconds
};

struct ExemplarCandidate {
  int64_t id;                                 // exemplar.id
  int64_t source_media_id;                    // media.id this candidate was cut from
  double start_s;                             // window start, seconds into the source recording
  double end_s;                               // window end, seconds into the source recording
  double confidence;                          // detector-assigned score in [0, 1]
  std::optional<RejectReason> reject_reason;  // set only for rejected candidates
};

// A window a human already ruled on, with the detector version that produced it.
// The population a new detector version's output is matched against so verdicts
// carry over instead of going back into the review queue.
struct ReviewedWindow {
  int64_t id;               // exemplar.id — superseded once its verdict is carried
  int64_t source_media_id;  // media.id the window belongs to
  double start_s;           // window start, seconds into the source recording
  double end_s;             // window end, seconds into the source recording
  bool approved;            // the human verdict being carried
  std::optional<RejectReason> reject_reason;  // set only when approved is false
  std::string detector_version;               // version that produced the window this was judged on
  std::optional<std::chrono::system_clock::time_point>
      reviewed_at;  // when the human ruled — a later verdict supersedes an earlier one
};

// One candidate window to write. `carried` is set only when the window matched a
// window a human already ruled on — otherwise the row lands pending.
struct CarriedVerdict {
  bool approved;                              // verdict inherited from the matched window
  std::optional<RejectReason> reject_reason;  // carried alongside a rejection
  std::string from_version;                   // detector version the verdict came from
};

struct ExemplarInsert {
  int64_t detector_run_id;                // run that produced this candidate
  int64_t source_media_id;                // media.id the window was cut from
  double start_s;                         // window start, seconds
  double end_s;                           // window end, seconds
  double confidence;                      // detector-assigned score in [0, 1]
  std::optional<CarriedVerdict> carried;  // absent — the row is pending review
};

struct MediaSourceInfo {
  int64_t id = 0;                                  // media.id
  std::string origin;                              // "youtube" / "freesound"
  CorpusRole corpus_role = CorpusRole::Reference;  // reference / curated
  std::string external_id;                         // origin-specific id (ytid / freesound id)
  std::optional<double> start_s;  // segment start within the source, for a multi-part video
  std::optional<double> end_s;    // segment end within the source, for a multi-part video
  std::string url;                // webpage_url / watch?v= / origin:external_id
  std::string title;              // freesound slug / yt video title (may be empty)
  std::string recorded_by;        // freesound username / yt uploader
  std::string group;              // sort/group key
  int part = 1;                   // 1-based part number within `group`
  int parts = 1;                  // total parts in `group`
  double length_s = 0;            // segment length when start/end known
};

struct FeatureFrameRow {
  double t_s;              // time from clip start
  double low_band_db;      // sub-band energy, dB
  double centroid_hz;      // sub-band spectral centroid (the drift cue)
  double spread_hz;        // sub-band spectral spread
  double dominance;        // sub-band / total energy fraction
  double stereo_corr;      // interaural L/R correlation at lag 0
  double ms_width;         // mid/side width — side / (mid + side)
  double phase_coherence;  // max L/R correlation over ITD lags (≤0.7 ms)
  double stereo_balance;   // R / (L + R) energy — 0.5 centred
};

struct ProcessedInsert {
  int64_t source_media_id;                        // media.id this sample was cut from
  CorpusRole corpus_role;                         // copied from the source media row
  std::string source_origin;                      // copied from the source media row
  std::string source_external_id;                 // copied from the source media row
  std::optional<std::string> source_title;        // copied from the source media row
  std::optional<std::string> source_recorded_by;  // contributor / uploader
  std::optional<std::string> source_url;          // copied from the source media row
  Domain domain;                                  // thunder / rain / wind / ocean
  double duration_s;                              // sample length, seconds
  int64_t size_bytes;                             // encoded FLAC size
  double start_s;                                 // window start in the source recording, seconds
  double end_s;                                   // window end in the source recording, seconds
  int64_t start_frame;                            // window start, sample index
  int64_t end_frame;                              // window end, sample index
  double confidence;             // detector confidence, carried through from the exemplar
  std::vector<std::byte> audio;  // the sliced sample, FLAC at the library rate
  std::string codec;             // always "flac" at present
  double noise_floor;            // dB re peak
  double peak;                   // linear peak amplitude
  double stereo_invariance;      // 0 until StereoProfile capture lands
  int children;                  // 0 — these are leaf samples
};

struct ProcessedRow {
  int64_t id;                              // processed.id
  int64_t source_media_id;                 // media.id this sample was cut from
  std::string external_id;                 // original source stem
  std::optional<std::string> title;        // contributor's title slug — metadata
  std::optional<std::string> recorded_by;  // contributor / uploader
  std::optional<std::string> url;          // source webpage_url, if any
  CorpusRole corpus_role;                  // reference / curated
  double duration_s;                       // sample length, seconds
  double confidence;                       // the detector confidence carried through extract
  int64_t size_bytes;                      // FLAC size
  double noise_floor;                      // dB re peak
  double peak;                             // linear peak amplitude
  double start_s;                          // window in the source recording
  double end_s;                            // window end in the source recording
};

struct BandRow {
  int band_index;  // 0-based band index within the sample's ParametricModel
  double amount;   // energy fraction
  double f_lo_hz;  // band lower bound, Hz
  double f_hi_hz;  // band upper bound, Hz
};

struct AdsrRow {
  double attack_s;       // 10% -> 90% of peak RMS
  double peak_s;         // offset from sound start to global RMS peak
  double release_s;      // 90% -> 10% of peak RMS
  double sustain_level;  // normalised 0–1 mean sustain level
};

struct LibraryCounts {
  int reference_media;  // media rows, corpus_role = reference
  int curated_media;    // media rows, corpus_role = curated
  int pending;          // exemplar approved IS NULL
  int approved;         // exemplar approved = true
  int rejected;         // exemplar approved = false
  int samples;          // processed rows
};

class MediaRepository {
 public:
  explicit MediaRepository(qr::Connection& db);
  MediaRepository(const MediaRepository&) = delete;
  MediaRepository& operator=(const MediaRepository&) = delete;
  std::vector<ReferenceMedia> fetch_reference(Domain domain) const;
  std::vector<CuratedMedia> fetch_curated(Domain domain) const;
  std::vector<MarkedMedia> fetch_marked_media(Domain domain) const;
  int64_t begin_detector_run(const std::string& detector_name, const std::string& detector_version,
                             Domain domain) const;
  std::size_t clear_unreviewed(Domain domain) const;
  std::size_t clear_unreviewed_for_media(const std::vector<int64_t>& media_ids) const;
  std::size_t clear_exemplars_for_media(const std::vector<int64_t>& media_ids) const;
  int64_t insert_exemplar(const ExemplarInsert& e) const;
  std::vector<ReviewedWindow> fetch_reviewed_windows(Domain domain) const;
  std::size_t delete_exemplars(const std::vector<int64_t>& exemplar_ids) const;
  void insert_exemplar_features(int64_t exemplar_id,
                                const std::vector<FeatureFrameRow>& frames) const;
  void finish_detector_run(int64_t run_id, int media_count, int candidate_count) const;
  std::unordered_set<int64_t> fetch_detected_media_ids(const std::string& detector_name,
                                                       const std::string& detector_version) const;
  void mark_detected(int64_t media_id, const std::string& detector_name,
                     const std::string& detector_version, int64_t detector_run_id, Domain domain,
                     int candidate_count) const;
  std::size_t clear_detections_for_media(const std::vector<int64_t>& media_ids,
                                         const std::string& detector_name,
                                         const std::string& detector_version) const;
  std::vector<ExemplarCandidate> fetch_unreviewed(Domain domain) const;
  std::vector<ExemplarCandidate> fetch_approved(Domain domain) const;
  std::vector<ExemplarCandidate> fetch_rejected(Domain domain) const;
  std::vector<std::byte> fetch_media_audio(int64_t media_id) const;
  void approve_exemplar(int64_t exemplar_id) const;
  void reject_exemplar(int64_t exemplar_id, RejectReason reason) const;
  std::vector<MediaSourceInfo> fetch_media_catalog() const;
  std::optional<int64_t> fetch_media_id(const std::string& origin,
                                        const std::string& external_id) const;
  LibraryCounts count_library(Domain domain) const;
  std::vector<MediaSourceInfo> fetch_recordings(Domain domain,
                                                std::optional<CorpusRole> role) const;
  std::vector<ProcessedRow> fetch_processed(Domain domain, std::optional<CorpusRole> role) const;
  std::vector<std::byte> fetch_processed_audio(int64_t processed_id) const;
  std::set<std::tuple<int64_t, int64_t, int64_t>> fetch_processed_windows(Domain domain) const;
  std::vector<BandRow> fetch_sample_bands(int64_t processed_id) const;
  std::optional<AdsrRow> fetch_sample_adsr(int64_t processed_id) const;
  std::size_t clear_processed(Domain domain) const;
  int64_t insert_processed(const ProcessedInsert& p) const;
  void insert_adsr(int64_t processed_id, double attack_s, double peak_s, double release_s,
                   double sustain_level) const;
  void insert_sample_band(int64_t processed_id, int band_index, double amount, double f_lo_hz,
                          double f_hi_hz) const;
  void store_resynth_score(int64_t processed_id, double similarity_pct) const;
  std::optional<double> fetch_resynth_score(int64_t processed_id) const;

 private:
  qr::Connection& db_;
};

}  // namespace sferic::io
