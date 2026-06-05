#include "io/parametric_json.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#include "core/logger.h"

namespace sferic {
namespace io {

using ojson = nlohmann::ordered_json;  // preserves insertion order in output

static const char* kind_to_str(analysis::DistributionKind k) {
  using K = analysis::DistributionKind;
  switch (k) {
    case K::UniformReal:
      return "UniformReal";
    case K::UniformInt:
      return "UniformInt";
    case K::Bernoulli:
      return "Bernoulli";
    case K::Binomial:
      return "Binomial";
    case K::NegativeBinomial:
      return "NegativeBinomial";
    case K::Geometric:
      return "Geometric";
    case K::Poisson:
      return "Poisson";
    case K::Exponential:
      return "Exponential";
    case K::Gamma:
      return "Gamma";
    case K::Weibull:
      return "Weibull";
    case K::ExtremeValue:
      return "ExtremeValue";
    case K::Normal:
      return "Normal";
    case K::Lognormal:
      return "Lognormal";
    case K::ChiSquared:
      return "ChiSquared";
    case K::Cauchy:
      return "Cauchy";
    case K::FisherF:
      return "FisherF";
    case K::StudentT:
      return "StudentT";
    case K::Discrete:
      return "Discrete";
    case K::PiecewiseConstant:
      return "PiecewiseConstant";
    case K::PiecewiseLinear:
      return "PiecewiseLinear";
  }
  return "Unknown";
}

static analysis::DistributionKind str_to_kind(const std::string& s) {
  using K = analysis::DistributionKind;
  if (s == "UniformReal") return K::UniformReal;
  if (s == "UniformInt") return K::UniformInt;
  if (s == "Bernoulli") return K::Bernoulli;
  if (s == "Binomial") return K::Binomial;
  if (s == "NegativeBinomial") return K::NegativeBinomial;
  if (s == "Geometric") return K::Geometric;
  if (s == "Poisson") return K::Poisson;
  if (s == "Exponential") return K::Exponential;
  if (s == "Gamma") return K::Gamma;
  if (s == "Weibull") return K::Weibull;
  if (s == "ExtremeValue") return K::ExtremeValue;
  if (s == "Normal") return K::Normal;
  if (s == "Lognormal") return K::Lognormal;
  if (s == "ChiSquared") return K::ChiSquared;
  if (s == "Cauchy") return K::Cauchy;
  if (s == "FisherF") return K::FisherF;
  if (s == "StudentT") return K::StudentT;
  if (s == "Discrete") return K::Discrete;
  if (s == "PiecewiseConstant") return K::PiecewiseConstant;
  if (s == "PiecewiseLinear") return K::PiecewiseLinear;
  std::unreachable();
}

// Params are written with their semantic names per kind, not as a raw array.
static ojson dist_to_json(const analysis::DistributionFit& d) {
  if (d.sample_count == 0) return nullptr;  // fit was skipped

  ojson j;
  j["kind"] = kind_to_str(d.kind);
  j["ks_p_value"] = d.ks_p_value;
  j["sample_count"] = d.sample_count;

  using K = analysis::DistributionKind;
  switch (d.kind) {
    case K::UniformReal:
    case K::UniformInt:
      j["a"] = d.params[0];
      j["b"] = d.params[1];
      break;
    case K::Bernoulli:
      j["p"] = d.params[0];
      break;
    case K::Binomial:
      j["n"] = d.params[0];
      j["p"] = d.params[1];
      break;
    case K::NegativeBinomial:
      j["k"] = d.params[0];
      j["p"] = d.params[1];
      break;
    case K::Geometric:
      j["p"] = d.params[0];
      break;
    case K::Poisson:
      j["mean"] = d.params[0];
      break;
    case K::Exponential:
      j["lambda"] = d.params[0];
      break;
    case K::Gamma:
      j["alpha"] = d.params[0];
      j["beta"] = d.params[1];
      break;
    case K::Weibull:
      j["shape"] = d.params[0];
      j["scale"] = d.params[1];
      break;
    case K::ExtremeValue:
      j["location"] = d.params[0];
      j["scale"] = d.params[1];
      break;
    case K::Normal:
      j["mean"] = d.params[0];
      j["stddev"] = d.params[1];
      break;
    case K::Lognormal:
      j["log_mean"] = d.params[0];
      j["log_stddev"] = d.params[1];
      break;
    case K::ChiSquared:
      j["degrees_of_freedom"] = d.params[0];
      break;
    case K::Cauchy:
      j["location"] = d.params[0];
      j["scale"] = d.params[1];
      break;
    case K::FisherF:
      j["df_m"] = d.params[0];
      j["df_n"] = d.params[1];
      break;
    case K::StudentT:
      j["degrees_of_freedom"] = d.params[0];
      break;
    case K::PiecewiseLinear:
      j["scale"] = d.params[0];  // absolute amplitude — denormalises peak-normalised knots
      j["weights"] = d.weights;
      break;
    case K::Discrete:
    case K::PiecewiseConstant:
      j["weights"] = d.weights;
      break;
  }

  return j;
}

static analysis::DistributionFit dist_from_json(const ojson& j) {
  analysis::DistributionFit d;
  if (j.is_null()) return d;  // sample_count stays 0

  d.kind = str_to_kind(j.at("kind").get<std::string>());
  d.ks_p_value = j.at("ks_p_value").get<double>();
  d.sample_count = j.at("sample_count").get<size_t>();

  using K = analysis::DistributionKind;
  switch (d.kind) {
    case K::UniformReal:
    case K::UniformInt:
      d.params[0] = j.at("a").get<double>();
      d.params[1] = j.at("b").get<double>();
      break;
    case K::Bernoulli:
      d.params[0] = j.at("p").get<double>();
      break;
    case K::Binomial:
      d.params[0] = j.at("n").get<double>();
      d.params[1] = j.at("p").get<double>();
      break;
    case K::NegativeBinomial:
      d.params[0] = j.at("k").get<double>();
      d.params[1] = j.at("p").get<double>();
      break;
    case K::Geometric:
      d.params[0] = j.at("p").get<double>();
      break;
    case K::Poisson:
      d.params[0] = j.at("mean").get<double>();
      break;
    case K::Exponential:
      d.params[0] = j.at("lambda").get<double>();
      break;
    case K::Gamma:
      d.params[0] = j.at("alpha").get<double>();
      d.params[1] = j.at("beta").get<double>();
      break;
    case K::Weibull:
      d.params[0] = j.at("shape").get<double>();
      d.params[1] = j.at("scale").get<double>();
      break;
    case K::ExtremeValue:
      d.params[0] = j.at("location").get<double>();
      d.params[1] = j.at("scale").get<double>();
      break;
    case K::Normal:
      d.params[0] = j.at("mean").get<double>();
      d.params[1] = j.at("stddev").get<double>();
      break;
    case K::Lognormal:
      d.params[0] = j.at("log_mean").get<double>();
      d.params[1] = j.at("log_stddev").get<double>();
      break;
    case K::ChiSquared:
      d.params[0] = j.at("degrees_of_freedom").get<double>();
      break;
    case K::Cauchy:
      d.params[0] = j.at("location").get<double>();
      d.params[1] = j.at("scale").get<double>();
      break;
    case K::FisherF:
      d.params[0] = j.at("df_m").get<double>();
      d.params[1] = j.at("df_n").get<double>();
      break;
    case K::StudentT:
      d.params[0] = j.at("degrees_of_freedom").get<double>();
      break;
    case K::PiecewiseLinear:
      d.params[0] = j.at("scale").get<double>();
      d.weights = j.at("weights").get<std::vector<double>>();
      break;
    case K::Discrete:
    case K::PiecewiseConstant:
      d.weights = j.at("weights").get<std::vector<double>>();
      break;
  }

  return d;
}

static ojson shape_to_json(const analysis::SpectralShape& s) {
  ojson j;
  j["centroid_hz"] = s.centroid_hz;
  j["spread_hz"] = s.spread_hz;
  j["tilt_db_oct"] = s.tilt_db_octave;
  j["flatness"] = s.flatness;
  j["rolloff_hz"] = s.rolloff_hz;
  return j;
}

static analysis::SpectralShape shape_from_json(const ojson& j) {
  analysis::SpectralShape s{};
  s.centroid_hz = j.at("centroid_hz").get<double>();
  s.spread_hz = j.at("spread_hz").get<double>();
  s.tilt_db_octave = j.at("tilt_db_oct").get<double>();
  s.flatness = j.at("flatness").get<double>();
  s.rolloff_hz = j.at("rolloff_hz").get<double>();
  return s;
}

// include_traj=false omits the per-frame rms_trajectory array (compact mode).
// All parameters (ADSR scalars, macro_shape, distributions, φ) are always written.
static ojson envelope_to_json(const analysis::TemporalEnvelope& e, bool include_traj) {
  ojson j;
  j["attack_time_s"] = e.attack_time_s;
  j["peak_time_s"] = e.peak_time_s;
  j["sustain_level"] = e.sustain_level;
  j["release_time_s"] = e.release_time_s;
  j["noise_floor_db"] = e.noise_floor_db;
  if (include_traj) j["rms_trajectory"] = e.rms_trajectory;
  j["macro_shape"] = dist_to_json(e.macro_shape);
  j["residual_dist"] = dist_to_json(e.residual_distribution);
  j["residual_autocorr"] = e.residual_autocorr;
  j["spike_interval_dist"] = dist_to_json(e.spike_interval_distribution);
  return j;
}

static analysis::TemporalEnvelope envelope_from_json(const ojson& j) {
  analysis::TemporalEnvelope e{};
  e.attack_time_s = j.at("attack_time_s").get<double>();
  e.peak_time_s = j.at("peak_time_s").get<double>();
  e.sustain_level = j.at("sustain_level").get<double>();
  e.release_time_s = j.at("release_time_s").get<double>();
  e.noise_floor_db = j.at("noise_floor_db").get<double>();
  if (j.contains("rms_trajectory"))  // absent in compact files
    e.rms_trajectory = j.at("rms_trajectory").get<std::vector<double>>();
  e.macro_shape = dist_from_json(j.at("macro_shape"));
  e.residual_distribution = dist_from_json(j.at("residual_dist"));
  e.residual_autocorr = j.at("residual_autocorr").get<double>();
  e.spike_interval_distribution = dist_from_json(j.at("spike_interval_dist"));
  return e;
}

static ojson band_to_json(const analysis::BandEnvelope& b, bool include_traj) {
  ojson j;
  j["center_hz"] = b.center_hz;
  j["bandwidth_hz"] = b.bandwidth_hz;
  j["bin_lo"] = b.bin_lo;
  j["bin_hi"] = b.bin_hi;
  j["energy_fraction"] = b.energy_fraction;
  j["mean_shape"] = shape_to_json(b.mean_shape);
  j["envelope"] = envelope_to_json(b.envelope, include_traj);
  if (include_traj) {
    j["spread_trajectory"] = b.spread_trajectory;
    j["q_trajectory"] = b.q_trajectory;
  }
  return j;
}

static analysis::BandEnvelope band_from_json(const ojson& j) {
  analysis::BandEnvelope b{};
  b.center_hz = j.at("center_hz").get<double>();
  b.bandwidth_hz = j.at("bandwidth_hz").get<double>();
  b.bin_lo = j.at("bin_lo").get<size_t>();
  b.bin_hi = j.at("bin_hi").get<size_t>();
  b.energy_fraction = j.at("energy_fraction").get<double>();
  b.mean_shape = shape_from_json(j.at("mean_shape"));
  b.envelope = envelope_from_json(j.at("envelope"));
  if (j.contains("spread_trajectory"))  // absent in compact files
    b.spread_trajectory = j.at("spread_trajectory").get<std::vector<double>>();
  if (j.contains("q_trajectory")) b.q_trajectory = j.at("q_trajectory").get<std::vector<double>>();
  return b;
}

static ojson track_to_json(const analysis::SpectralPeakTrack& t) {
  ojson j;
  j["birth_time_s"] = t.birth_time_s;
  j["death_time_s"] = t.death_time_s;
  j["initial_freq_hz"] = t.initial_frequency_hz;
  j["initial_amp_db"] = t.initial_amplitude_db;
  j["q_factor"] = t.q_factor;
  j["freq_drift_hz_s"] = t.frequency_drift_hz_per_s;
  j["freq_jitter"] = dist_to_json(t.frequency_jitter);
  j["amp_variation"] = dist_to_json(t.amplitude_variation);
  return j;
}

static analysis::SpectralPeakTrack track_from_json(const ojson& j) {
  analysis::SpectralPeakTrack t{};
  t.birth_time_s = j.at("birth_time_s").get<double>();
  t.death_time_s = j.at("death_time_s").get<double>();
  t.initial_frequency_hz = j.at("initial_freq_hz").get<double>();
  t.initial_amplitude_db = j.at("initial_amp_db").get<double>();
  t.q_factor = j.at("q_factor").get<double>();
  t.frequency_drift_hz_per_s = j.at("freq_drift_hz_s").get<double>();
  t.frequency_jitter = dist_from_json(j.at("freq_jitter"));
  t.amplitude_variation = dist_from_json(j.at("amp_variation"));
  return t;
}

void save_parametric_json(const analysis::ParametricModel& model, const std::string& path,
                          TrajectoryMode mode) {
  SFERIC_SCOPE("save_parametric_json");

  const bool include_traj = mode == TrajectoryMode::Full;

  ojson j;
  j["_format"] = "sferic_parametric_model";
  j["_version"] = 1;
  j["_trajectories"] = include_traj ? "full" : "compact";
  j["sample_rate"] = model.sample_rate;
  j["duration_s"] = model.duration_s;
  j["source_frame_count"] = model.source_frame_count;
  j["num_bins"] = model.num_bins;
  j["overall_envelope"] = envelope_to_json(model.overall_envelope, include_traj);
  j["mean_spectral_shape"] = shape_to_json(model.mean_spectral_shape);

  // spectral_trajectory — per-frame shape; omitted in compact mode
  if (include_traj) {
    ojson traj = ojson::array();
    for (const auto& s : model.spectral_trajectory) traj.push_back(shape_to_json(s));
    j["spectral_trajectory"] = std::move(traj);
  }

  // bands
  ojson bands = ojson::array();
  for (const auto& b : model.bands) bands.push_back(band_to_json(b, include_traj));
  j["bands"] = std::move(bands);

  // peak tracks
  ojson tracks = ojson::array();
  for (const auto& t : model.peak_tracks) tracks.push_back(track_to_json(t));
  j["peak_tracks"] = std::move(tracks);

  // top-level distributions
  j["centroid_dist"] = dist_to_json(model.centroid_distribution);
  j["spread_dist"] = dist_to_json(model.spread_distribution);
  j["amp_dist"] = dist_to_json(model.amplitude_distribution);

  std::ofstream f(path);
  f << j.dump(2);  // 2-space indent

  {
    std::ostringstream ss;
    ss << "wrote " << path << "  mode=" << (include_traj ? "full" : "compact")
       << "  frames=" << model.source_frame_count << "  bands=" << model.bands.size()
       << "  peak_tracks=" << model.peak_tracks.size();
    SFERIC_LOG(Info, ss.str());
  }
}

analysis::ParametricModel load_parametric_json(const std::string& path) {
  SFERIC_SCOPE("load_parametric_json");

  std::ifstream f(path);
  ojson j = ojson::parse(f);

  analysis::ParametricModel model;
  model.sample_rate = j.at("sample_rate").get<double>();
  model.duration_s = j.at("duration_s").get<double>();
  model.source_frame_count = j.at("source_frame_count").get<size_t>();
  model.num_bins = j.at("num_bins").get<size_t>();
  model.overall_envelope = envelope_from_json(j.at("overall_envelope"));
  model.mean_spectral_shape = shape_from_json(j.at("mean_spectral_shape"));

  if (j.contains("spectral_trajectory"))  // absent in compact files
    for (const auto& s : j.at("spectral_trajectory"))
      model.spectral_trajectory.push_back(shape_from_json(s));
  for (const auto& b : j.at("bands")) model.bands.push_back(band_from_json(b));
  for (const auto& t : j.at("peak_tracks")) model.peak_tracks.push_back(track_from_json(t));

  model.centroid_distribution = dist_from_json(j.at("centroid_dist"));
  model.spread_distribution = dist_from_json(j.at("spread_dist"));
  model.amplitude_distribution = dist_from_json(j.at("amp_dist"));

  {
    std::ostringstream ss;
    ss << "loaded " << path << "  frames=" << model.source_frame_count
       << "  bands=" << model.bands.size() << "  peak_tracks=" << model.peak_tracks.size();
    SFERIC_LOG(Info, ss.str());
  }

  return model;
}

}  // namespace io
}  // namespace sferic
