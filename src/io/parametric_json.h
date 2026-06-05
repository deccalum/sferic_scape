#pragma once

#include <string>

#include "analysis/parametric_model.h"

namespace sferic {
namespace io {

// Serialises every field of ParametricModel to a human-readable JSON file.
// Field names match the C++ struct member names exactly.
// Distribution parameters are written with their semantic names (e.g. "mean",
// "stddev") rather than a raw params[0..3] array so the file is self-documenting.
// Trajectories (rms, spectral, spread, Q) are written as flat number arrays —
// one value per source analysis frame.

// Trajectory inclusion policy.
//   Full    — every per-frame array (rms, spectral, spread, Q). Use for analysis
//             and side-by-side visual comparison against the original recording.
//   Compact — parameters only: macro_shape, fitted distributions, ADSR, band
//             metadata. The distributable preset; the synth reconstructs envelopes
//             from these alone (see ParametricSynth). Typically a few KB.
enum class TrajectoryMode { Full, Compact };

void save_parametric_json(const analysis::ParametricModel& model,
                          const std::string& path,
                          TrajectoryMode mode = TrajectoryMode::Full);

// Loads either mode. Missing per-frame arrays (compact files) deserialise to
// empty vectors — the model remains synthesizable from its parameters.
analysis::ParametricModel load_parametric_json(const std::string& path);

}  // namespace io
}  // namespace sferic
