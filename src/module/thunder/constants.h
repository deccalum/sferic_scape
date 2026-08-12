#pragma once

// Thunder acoustics constants distilled from docs/THUNDER_PAPERS.md

namespace sferic::thunder {

constexpr double FUNDAMENTAL_CEIL_HZ = 230.0;
constexpr double PEAK_LO_HZ = 42.0;
constexpr double PEAK_HI_HZ = 137.0;
constexpr double CLAP_MIN_S = 0.05;
constexpr double CLAP_MAX_S = 0.30;
constexpr double PEAL_MAX_S = 2.0;
constexpr double RUMBLE_MIN_S = 2.0;
constexpr double RUMBLE_MAX_S = 30.0;
constexpr double ONSET_DB_RISE = 20.0;

}  // namespace sferic::thunder
