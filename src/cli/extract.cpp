#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "analysis/parametric_model.h"
#include "analysis/prepare.h"
#include "analysis/spectral_envelope.h"
#include "cli/commands.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "io/audio_file.h"

namespace sferic::cli {

void extract(io::MediaRepository& repo, Domain domain, ExtractOptions opts) {
  SFERIC_SCOPE("cli::extract");

  const std::vector<io::ExemplarCandidate> approved = repo.fetch_approved(domain);
  if (approved.empty()) {
    SFERIC_LOG(Warn, "no approved exemplars in: " + std::string(to_string(domain)));
    return;
  }
  const std::vector<io::MediaSourceInfo> catalog = repo.fetch_media_catalog();
  std::unordered_map<int64_t, const io::MediaSourceInfo*> by_media;
  by_media.reserve(catalog.size());
  for (const io::MediaSourceInfo& m : catalog) by_media[m.id] = &m;
  std::set<std::tuple<int64_t, int64_t, int64_t>> seen;
  if (opts.force) {
    const std::size_t cleared = repo.clear_processed(domain);
    SFERIC_LOG(Info, "cleared processed rows: " + std::to_string(cleared));
  } else {
    seen = repo.fetch_processed_windows(domain);
    SFERIC_LOG(Info, "already in the library: " + std::to_string(seen.size()));
  }
  int64_t cached_media_id = -1;
  AudioBuffer media;
  size_t extracted = 0;
  size_t skipped_existing = 0;
  for (size_t i = 0; i < approved.size(); ++i) {
    const io::ExemplarCandidate& e = approved[i];
    const io::MediaSourceInfo& src = *by_media.at(e.source_media_id);
    const auto window = std::make_tuple(e.source_media_id, std::llround(e.start_s * 1000.0),
                                        std::llround(e.end_s * 1000.0));
    if (!seen.insert(window).second) {
      ++skipped_existing;
      continue;
    }
    if (e.source_media_id != cached_media_id) {
      const std::vector<std::byte> bytes = repo.fetch_media_audio(e.source_media_id);
      media = io::decode(std::span<const std::byte>(bytes));
      cached_media_id = e.source_media_id;
    }
    const analysis::prepare::PreparedClip prep =
        analysis::prepare::materialize(media, e.start_s, e.end_s);
    const AudioBuffer& sample = prep.mono;
    const double sr = sample.sample_rate();
    std::vector<std::byte> flac = io::encode_flac(sample);
    const int64_t flac_bytes = static_cast<int64_t>(flac.size());
    const analysis::SpectralEnvelope spectral =
        analysis::SpectralAnalyzer(2048, 0, 15).analyze(sample);
    const analysis::ParametricModel model = analysis::ParametricExtractor().extract(spectral);
    const analysis::TemporalEnvelope& env = model.overall_envelope;
    const double stereo_inv = prep.stereo ? prep.stereo->mean_corr : 0.0;
    io::ProcessedInsert p{
        .source_media_id = e.source_media_id,
        .corpus_role = src.corpus_role,
        .source_origin = src.origin,
        .source_external_id = src.external_id,
        .source_title = src.title.empty() ? std::nullopt : std::optional<std::string>(src.title),
        .source_recorded_by =
            src.recorded_by.empty() ? std::nullopt : std::optional<std::string>(src.recorded_by),
        .source_url = src.url.empty() ? std::nullopt : std::optional<std::string>(src.url),
        .domain = domain,
        .duration_s = sample.duration(),
        .size_bytes = flac_bytes,
        .start_s = e.start_s,
        .end_s = e.end_s,
        .start_frame = static_cast<int64_t>(e.start_s * sr),
        .end_frame = static_cast<int64_t>(e.end_s * sr),
        .confidence = e.confidence,
        .audio = std::move(flac),
        .codec = "flac",
        .noise_floor = env.noise_floor_db,
        .peak = static_cast<double>(sample.peak_amplitude()),
        .stereo_invariance = stereo_inv,
        .children = 0};
    const int64_t pid = repo.insert_processed(p);
    repo.insert_adsr(pid, env.attack_time_s, env.peak_time_s, env.release_time_s,
                     env.sustain_level);
    const double freq_spacing =
        model.num_bins > 1 ? model.sample_rate * 0.5 / static_cast<double>(model.num_bins - 1)
                           : 0.0;
    for (size_t b = 0; b < model.bands.size(); ++b) {
      const analysis::BandEnvelope& band = model.bands[b];
      const double f_lo = static_cast<double>(band.bin_lo) * freq_spacing;
      const double f_hi = static_cast<double>(band.bin_hi + 1) * freq_spacing;
      repo.insert_sample_band(pid, static_cast<int>(b), band.energy_fraction, f_lo, f_hi);
    }
    ++extracted;
    SFERIC_LOG(
        Info, "processed " + std::to_string(pid) + " — " + std::to_string(model.bands.size()) +
                  " bands, attack " + std::to_string(env.attack_time_s) + "s, peak " +
                  std::to_string(env.peak_time_s) + "s, gain " + std::to_string(prep.gain_applied));
    SFERIC_PROGRESS(i + 1, approved.size(), sample.duration() * 1000.0, flac_bytes);
  }

  SFERIC_LOG(Info, "extract complete — " + std::to_string(extracted) + " new samples for domain=" +
                       std::string(to_string(domain)) + " (" + std::to_string(skipped_existing) +
                       " already in library / duplicate windows skipped)");
}

}  // namespace sferic::cli
