#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <qr/qr.hpp>
#include <string>
#include <vector>

#include "core/domain.h"

namespace sferic::io::schema {

struct Media {
  static constexpr std::string_view table_name = "media";

  static constexpr qr::Column<Media, int64_t> id{"id"};
  static constexpr qr::Column<Media, std::string> origin{"origin"};
  static constexpr qr::Column<Media, CorpusRole> corpus_role{"corpus_role"};
  static constexpr qr::Column<Media, std::string> external_id{"external_id"};
  static constexpr qr::Column<Media, std::optional<double>> start_s{"start_s"};
  static constexpr qr::Column<Media, std::optional<double>> end_s{"end_s"};
  static constexpr qr::Column<Media, int> sample_rate{"sample_rate"};
  static constexpr qr::Column<Media, int> channels{"channels"};
  static constexpr qr::Column<Media, std::string> codec{"codec"};
  static constexpr qr::Column<Media, std::vector<std::byte>> audio{"audio"};
  static constexpr qr::Column<Media, std::optional<int>> byte_size{"byte_size"};
  static constexpr qr::Column<Media, std::optional<std::vector<double>>> marks{"marks"};

  static constexpr auto columns = std::make_tuple(id, origin, corpus_role, external_id, start_s, end_s, sample_rate, channels, codec, byte_size);
};

struct YtVideo {
  static constexpr std::string_view table_name = "ytvideo";

  static constexpr qr::Column<YtVideo, int64_t> media_id{"media_id"};
  static constexpr qr::Column<YtVideo, std::string> ytid{"ytid"};
  static constexpr qr::Column<YtVideo, std::optional<std::string>> title{"title"};
  static constexpr qr::Column<YtVideo, std::optional<std::string>> webpage_url{"webpage_url"};

  static constexpr auto columns = std::make_tuple(media_id, ytid, title, webpage_url);
};

struct FreeSound {
  static constexpr std::string_view table_name = "freesound";

  static constexpr qr::Column<FreeSound, int64_t> media_id{"media_id"};
  static constexpr qr::Column<FreeSound, std::optional<std::string>> title{"title"};
  static constexpr qr::Column<FreeSound, std::optional<std::string>> username{"username"};
  static constexpr qr::Column<FreeSound, std::optional<std::string>> webpage_url{"webpage_url"};

  static constexpr auto columns = std::make_tuple(media_id, title, username, webpage_url);
};

struct MediaLabels {
  static constexpr std::string_view table_name = "media_labels";

  static constexpr qr::Column<MediaLabels, int64_t> media_id{"media_id"};
  static constexpr qr::Column<MediaLabels, int64_t> label_id{"label_id"};

  static constexpr auto columns = std::make_tuple(media_id, label_id);
};

struct Labels {
  static constexpr std::string_view table_name = "labels";

  static constexpr qr::Column<Labels, int64_t> id{"id"};
  static constexpr qr::Column<Labels, std::string> domain{"domain"};

  static constexpr auto columns = std::make_tuple(id, domain);
};

struct DetectorRun {
  static constexpr std::string_view table_name = "detector_run";

  static constexpr qr::Column<DetectorRun, int64_t> id{"id"};
  static constexpr qr::Column<DetectorRun, std::string> detector_name{"detector_name"};
  static constexpr qr::Column<DetectorRun, std::string> detector_version{"detector_version"};
  static constexpr qr::Column<DetectorRun, Domain> domain{"domain"};
  static constexpr qr::Column<DetectorRun, std::optional<std::chrono::system_clock::time_point>> finished_at{"finished_at"};
  static constexpr qr::Column<DetectorRun, int> media_count{"media_count"};
  static constexpr qr::Column<DetectorRun, int> candidate_count{"candidate_count"};

  static constexpr auto columns = std::make_tuple(id, detector_name, detector_version, domain, finished_at, media_count, candidate_count);
};

struct Log {
  static constexpr std::string_view table_name = "log";

  static constexpr qr::Column<Log, int64_t> id{"id"};
  static constexpr qr::Column<Log, std::string> run{"run"};
  static constexpr qr::Column<Log, std::string> level{"level"};
  static constexpr qr::Column<Log, std::string> func{"func"};
  static constexpr qr::Column<Log, std::string> message{"message"};
  static constexpr qr::Column<Log, std::chrono::system_clock::time_point> logged_at{"logged_at"};

  static constexpr auto columns = std::make_tuple(id, run, level, func, message, logged_at);
};

struct Exemplar {
  static constexpr std::string_view table_name = "exemplar";

  static constexpr qr::Column<Exemplar, int64_t> id{"id"};
  static constexpr qr::Column<Exemplar, int64_t> source_media_id{"source_media_id"};
  static constexpr qr::Column<Exemplar, int64_t> detector_run_id{"detector_run_id"};
  static constexpr qr::Column<Exemplar, double> start_s{"start_s"};
  static constexpr qr::Column<Exemplar, double> end_s{"end_s"};
  static constexpr qr::Column<Exemplar, std::optional<double>> confidence{"confidence"};
  static constexpr qr::Column<Exemplar, std::optional<bool>> approved{"approved"};
  static constexpr qr::Column<Exemplar, std::optional<std::chrono::system_clock::time_point>> reviewed_at{"reviewed_at"};
  static constexpr qr::Column<Exemplar, std::optional<RejectReason>> reject_reason{"reject_reason"};
  static constexpr qr::Column<Exemplar, std::optional<std::string>> carried_from{"carried_from"};

  static constexpr auto columns = std::make_tuple(id, source_media_id, detector_run_id, start_s, end_s, confidence, approved, reviewed_at, reject_reason, carried_from);
};

struct ExemplarFeature {
  static constexpr std::string_view table_name = "exemplar_feature";

  static constexpr qr::Column<ExemplarFeature, int64_t> exemplar_id{"exemplar_id"};
  static constexpr qr::Column<ExemplarFeature, int> frame_index{"frame_index"};
  static constexpr qr::Column<ExemplarFeature, double> t_s{"t_s"};
  static constexpr qr::Column<ExemplarFeature, double> low_band_db{"low_band_db"};
  static constexpr qr::Column<ExemplarFeature, double> centroid_hz{"centroid_hz"};
  static constexpr qr::Column<ExemplarFeature, double> spread_hz{"spread_hz"};
  static constexpr qr::Column<ExemplarFeature, double> dominance{"dominance"};
  static constexpr qr::Column<ExemplarFeature, double> stereo_corr{"stereo_corr"};
  static constexpr qr::Column<ExemplarFeature, double> ms_width{"ms_width"};
  static constexpr qr::Column<ExemplarFeature, double> phase_coherence{"phase_coherence"};
  static constexpr qr::Column<ExemplarFeature, double> stereo_balance{"stereo_balance"};

  static constexpr auto columns = std::make_tuple(exemplar_id, frame_index, t_s, low_band_db, centroid_hz, spread_hz, dominance,stereo_corr, ms_width, phase_coherence, stereo_balance);
};

struct MediaDetection {
  static constexpr std::string_view table_name = "media_detection";

  static constexpr qr::Column<MediaDetection, int64_t> media_id{"media_id"};
  static constexpr qr::Column<MediaDetection, std::string> detector_name{"detector_name"};
  static constexpr qr::Column<MediaDetection, std::string> detector_version{"detector_version"};
  static constexpr qr::Column<MediaDetection, int64_t> detector_run_id{"detector_run_id"};
  static constexpr qr::Column<MediaDetection, Domain> domain{"domain"};
  static constexpr qr::Column<MediaDetection, int> candidate_count{"candidate_count"};

  static constexpr auto columns = std::make_tuple(media_id, detector_name, detector_version, detector_run_id, domain, candidate_count);
};

struct Processed {
  static constexpr std::string_view table_name = "processed";

  static constexpr qr::Column<Processed, int64_t> id{"id"};
  static constexpr qr::Column<Processed, CorpusRole> corpus_role{"corpus_role"};
  static constexpr qr::Column<Processed, int64_t> source_media_id{"source_media_id"};
  static constexpr qr::Column<Processed, std::string> source_origin{"source_origin"};
  static constexpr qr::Column<Processed, std::string> source_external_id{"source_external_id"};
  static constexpr qr::Column<Processed, std::optional<std::string>> source_title{"source_title"};
  static constexpr qr::Column<Processed, std::optional<std::string>> source_recorded_by{"source_recorded_by"};
  static constexpr qr::Column<Processed, std::optional<std::string>> source_url{"source_url"};
  static constexpr qr::Column<Processed, Domain> domain{"domain"};
  static constexpr qr::Column<Processed, double> duration_s{"duration_s"};
  static constexpr qr::Column<Processed, int64_t> size_bytes{"size_bytes"};
  static constexpr qr::Column<Processed, double> start_s{"start_s"};
  static constexpr qr::Column<Processed, double> end_s{"end_s"};
  static constexpr qr::Column<Processed, int64_t> start_frame{"start_frame"};
  static constexpr qr::Column<Processed, int64_t> end_frame{"end_frame"};
  static constexpr qr::Column<Processed, double> confidence{"confidence"};
  static constexpr qr::Column<Processed, std::vector<std::byte>> audio{"audio"};
  static constexpr qr::Column<Processed, std::string> codec{"codec"};
  static constexpr qr::Column<Processed, double> noise_floor{"noise_floor"};
  static constexpr qr::Column<Processed, double> peak{"peak"};
  static constexpr qr::Column<Processed, double> stereo_invariance{"stereo_invariance"};
  static constexpr qr::Column<Processed, int> children{"children"};

  static constexpr auto columns = std::make_tuple(id, corpus_role, source_media_id, source_origin, source_external_id, source_title, source_recorded_by, source_url, domain, duration_s, size_bytes, start_s, end_s, confidence, codec, noise_floor, peak, stereo_invariance, children);
};

struct Adsr {
  static constexpr std::string_view table_name = "adsr";

  static constexpr qr::Column<Adsr, int64_t> processed_id{"processed_id"};
  static constexpr qr::Column<Adsr, double> peak_s{"peak_s"};
  static constexpr qr::Column<Adsr, double> attack_s{"attack_s"};
  static constexpr qr::Column<Adsr, double> release_s{"release_s"};
  static constexpr qr::Column<Adsr, double> sustain_level{"sustain_level"};

  static constexpr auto columns = std::make_tuple(processed_id, peak_s, attack_s, release_s, sustain_level);
};

struct ParametricModel {
  static constexpr std::string_view table_name = "parametric_model";

  static constexpr qr::Column<ParametricModel, int64_t> processed_id{"processed_id"};
  static constexpr qr::Column<ParametricModel, double> resynth_similarity{"resynth_similarity"};
  static constexpr qr::Column<ParametricModel, std::optional<std::chrono::system_clock::time_point>>analyzed_at{"analyzed_at"};

  static constexpr auto columns = std::make_tuple(processed_id, resynth_similarity, analyzed_at);
};

struct SampleBand {
  static constexpr std::string_view table_name = "sample_band";

  static constexpr qr::Column<SampleBand, int64_t> processed_id{"processed_id"};
  static constexpr qr::Column<SampleBand, int> band_index{"band_index"};
  static constexpr qr::Column<SampleBand, double> amount{"amount"};
  static constexpr qr::Column<SampleBand, double> f_lo_hz{"f_lo_hz"};
  static constexpr qr::Column<SampleBand, double> f_hi_hz{"f_hi_hz"};

  static constexpr auto columns = std::make_tuple(processed_id, band_index, amount, f_lo_hz, f_hi_hz);
};

}  // namespace sferic::io::schema
