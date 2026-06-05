#pragma once

#include <array>
#include <vector>

namespace sferic {
namespace thunder {

// One lightning discharge as a physical emission event.
// Geometry is in the listener frame (+x right, +y forward, +z up).
struct Strike {
  double current_ka;        // discharge current (kA) — drives source overpressure
  double energy_j_per_m;    // acoustic energy per unit channel length — drives f_peak
  double t_emit_s;          // storm-clock emission time

  std::array<double, 3> origin_m;                   // formation point (cloud attachment)
  std::vector<std::array<double, 3>> endpoints;     // ground contacts + branch ends
  std::vector<std::array<double, 3>> channel_path;  // tortuous path of least resistance
};

}  // namespace thunder
}  // namespace sferic
