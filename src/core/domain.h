#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sferic {

enum class Domain : uint8_t { Thunder, Rain, Wind, Ocean };
enum class CorpusRole : uint8_t { Reference, Curated };

constexpr std::string_view to_string(Domain d) {
  switch (d) {
    case Domain::Thunder:
      return "thunder";
    case Domain::Rain:
      return "rain";
    case Domain::Wind:
      return "wind";
    case Domain::Ocean:
      return "ocean";
  }
  std::unreachable();
}

inline std::optional<Domain> try_from_string(std::string_view s, std::type_identity<Domain>) {
  if (s == "thunder") return Domain::Thunder;
  if (s == "rain") return Domain::Rain;
  if (s == "wind") return Domain::Wind;
  if (s == "ocean") return Domain::Ocean;
  return std::nullopt;
}

inline Domain from_string(std::string_view s, std::type_identity<Domain> tag) {
  if (const std::optional<Domain> d = try_from_string(s, tag)) return *d;
  throw std::invalid_argument("unknown Domain: " + std::string(s));
}

constexpr std::string_view to_string(CorpusRole r) {
  switch (r) {
    case CorpusRole::Reference:
      return "reference";
    case CorpusRole::Curated:
      return "curated";
  }
  std::unreachable();
}

inline CorpusRole from_string(std::string_view s, std::type_identity<CorpusRole>) {
  if (s == "reference") return CorpusRole::Reference;
  if (s == "curated") return CorpusRole::Curated;
  throw std::invalid_argument("unknown CorpusRole: " + std::string(s));
}

// rejected exemplar reason at review
//
//  - Noise:       not a real event — drop entirely
//  - Unsort:      flag as unsorted, can be used for another domain
//  - BadWindow:   real event, mis-cut — a longer window
//  - BadQuality:  audible but too low-fidelity to model
enum class RejectReason : uint8_t { Noise, Unsort, BadWindow, BadQuality };

constexpr std::string_view to_string(RejectReason r) {
  switch (r) {
    case RejectReason::Noise:
      return "noise";
    case RejectReason::Unsort:
      return "unsort";
    case RejectReason::BadWindow:
      return "bad_window";
    case RejectReason::BadQuality:
      return "bad_quality";
  }
  std::unreachable();
}

inline RejectReason from_string(std::string_view s, std::type_identity<RejectReason>) {
  if (s == "noise") return RejectReason::Noise;
  if (s == "unsort") return RejectReason::Unsort;
  if (s == "bad_window") return RejectReason::BadWindow;
  if (s == "bad_quality") return RejectReason::BadQuality;
  throw std::invalid_argument("unknown RejectReason: " + std::string(s));
}

}  // namespace sferic
