#include "io/data_domain.h"

#include <yaml-cpp/yaml.h>

#include <map>
#include <sstream>

#include "core/logger.h"

namespace sferic {
namespace io {

double parse_segment_seconds(std::string id) {
  if (id.ends_with(".wav")) id.resize(id.size() - 4);
  const auto underscore = id.find('_');
  if (underscore != std::string::npos) {
    // SSSs_MMMms
    const double sec = std::stod(id.substr(0, id.find('s')));
    const double ms = std::stod(id.substr(underscore + 1, id.find("ms", underscore) - underscore - 1));
    return sec + ms / 1000.0;
  }
}

namespace {

std::vector<std::string> as_string_list(const YAML::Node& node) {
  if (node.IsSequence()) {
    std::vector<std::string> out;
    for (const auto& item : node) out.push_back(item.as<std::string>());
    return out;
  }
  std::vector<std::string> out;
  std::istringstream ss(node.as<std::string>());
  std::string token;
  while (std::getline(ss, token, ',')) {
    const auto a = token.find_first_not_of(" \t");
    const auto b = token.find_last_not_of(" \t");
    if (a != std::string::npos) out.push_back(token.substr(a, b - a + 1));
  }
  return out;
}

// notes may be a scalar or a list — join a list with "; " into one line.
std::string read_notes(const YAML::Node& node) {
  if (!node) return "";
  if (!node.IsSequence()) return node.as<std::string>();
  std::string out;
  for (const auto& item : node) {
    if (!out.empty()) out += "; ";
    out += item.as<std::string>();
  }
  return out;
}

const std::map<std::string, Segment> kSegmentMap = {
    {"full", Segment::Full},
    {"attack", Segment::Attack},
    {"body", Segment::Body},
    {"tail", Segment::Tail},
};

const std::map<std::string, FrequencyBias> kFreqBiasMap = {
    {"sub", FrequencyBias::Sub},
    {"mid", FrequencyBias::Mid},
    {"high", FrequencyBias::High},
};

template <typename E>
std::vector<E> map_enum(const std::vector<std::string>& strs,
                        const std::map<std::string, E>& table) {
  std::vector<E> out;
  out.reserve(strs.size());
  for (const auto& s : strs) out.push_back(table.at(s));  // std::out_of_range on unknown value
  return out;
}

DomainData parse_thunder(const YAML::Node& seg) {
  ThunderData d;
  d.seg_type = map_enum(as_string_list(seg["seg-type"]), kSegmentMap);
  d.freq_bias = map_enum(as_string_list(seg["freq-bias"]), kFreqBiasMap);
  d.sub_strikes = seg["sub-strikes"] ? seg["sub-strikes"].as<int>() : 0;
  d.pre_crack = seg["pre-crack"] ? seg["pre-crack"].as<bool>() : false;
  d.stereo_fx = seg["stereo-fx"] ? seg["stereo-fx"].as<bool>() : false;
  return d;
}

DomainData parse_rain(const YAML::Node& seg) {
  RainData d;
  d.loopable = seg["loopable"] ? seg["loopable"].as<bool>() : false;
  return d;
}

DomainData parse_wind(const YAML::Node& seg) {
  WindData d;
  d.intensity = seg["intensity"] ? seg["intensity"].as<std::string>() : "";
  return d;
}

DomainData parse_ocean(const YAML::Node& /*seg*/) { return OceanData{}; }

using DomainParser = DomainData (*)(const YAML::Node&);
const std::map<std::string, DomainParser> kDomainParsers = {
    {"thunder", parse_thunder},
    {"rain", parse_rain},
    {"wind", parse_wind},
    {"ocean", parse_ocean},
};

// The original uncut recording in rec_dir, named <rec_id>__*.wav — or empty if absent.
std::string find_source(const std::filesystem::path& rec_dir, const std::string& rec_id) {
  const std::string prefix = rec_id + "__";
  for (const auto& de : std::filesystem::directory_iterator(rec_dir)) {
    if (de.path().extension() == ".wav" && de.path().stem().string().starts_with(prefix))
      return de.path().string();
  }
  return "";
}

void parse_yml(const std::filesystem::path& yml_path, std::vector<LibraryEntry>& out) {
  SFERIC_SCOPE("parse_yml");
  SFERIC_LOG(Info, "loading: " + yml_path.string());

  const YAML::Node root = YAML::LoadFile(yml_path.string());

  // Domain — explicit `domain:` key, falling back to the file stem (thunder.yml → thunder).
  const std::string domain = root["domain"] ? root["domain"].as<std::string>()
                                            : yml_path.stem().string();
  const auto parser_it = kDomainParsers.find(domain);
  if (parser_it == kDomainParsers.end()) {
    SFERIC_LOG(Info, "no domain parser for: " + domain + " — skipping");
    return;
  }
  const DomainParser parse_segment = parser_it->second;

  const std::filesystem::path dir = yml_path.parent_path();

  for (const auto& rec_kv : root["recordings"]) {
    const std::string rec_id = rec_kv.first.as<std::string>();
    const YAML::Node& rec    = rec_kv.second;

    const std::filesystem::path rec_dir = dir / rec_id;

    // Source — explicit `source:` filename, else auto-detected <rec_id>__*.wav.
    const std::string source = rec["source"] ? (rec_dir / rec["source"].as<std::string>()).string()
                                             : find_source(rec_dir, rec_id);
    const int sr_khz = rec["SR"] ? rec["SR"].as<int>() : 0;
    const std::string rec_notes = read_notes(rec["notes"]);
    SFERIC_LOG(Info, domain + "/" + rec_id + " source: " + (source.empty() ? "(none)" : source));

    for (const auto& seg_kv : rec["segments"]) {
      const std::string seg_id = seg_kv.first.as<std::string>();
      const YAML::Node& seg    = seg_kv.second;
      const std::string wav_name = seg_id.ends_with(".wav") ? seg_id : seg_id + ".wav";

      LibraryEntry entry;
      entry.wav_path        = rec_dir / wav_name;
      entry.recording_id    = rec_id;
      entry.segment_id      = seg_id;
      entry.sample_rate_khz = sr_khz;
      entry.process         = seg["process"] ? seg["process"].as<bool>() : false;
      entry.segment_start_s = parse_segment_seconds(seg_id);
      entry.source          = source;
      entry.rating          = seg["rating"] ? seg["rating"].as<int>() : 0;
      entry.notes           = seg["notes"] ? read_notes(seg["notes"]) : rec_notes;
      entry.payload         = parse_segment(seg);

      SFERIC_LOG(Info, rec_id + "/" + seg_id);
      out.push_back(std::move(entry));
    }
  }
}

}  // namespace

std::vector<LibraryEntry> load_library(const std::filesystem::path& root) {
  SFERIC_SCOPE("load_library");
  SFERIC_LOG(Info, "scanning: " + root.string());

  std::vector<LibraryEntry> result;
  for (const auto& de : std::filesystem::recursive_directory_iterator(root)) {
    if (de.path().extension() == ".yml") parse_yml(de.path(), result);
  }

  // No yml inside root — walk up to the nearest ancestor holding a domain yml (root is a
  // subtree it describes). Lets you point at a recording folder and still load its metadata.
  for (std::filesystem::path dir = root.parent_path(); !dir.empty() && result.empty();
       dir = dir.parent_path()) {
    for (const auto& de : std::filesystem::directory_iterator(dir)) {
      if (de.path().extension() == ".yml") parse_yml(de.path(), result);
    }
    if (dir == dir.root_path()) break;  // reached filesystem root — stop
  }

  SFERIC_LOG(Info, "entries: " + std::to_string(result.size()));
  return result;
}

}  // namespace io
}  // namespace sferic
