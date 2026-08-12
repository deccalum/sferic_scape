#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/buffer.h"

namespace sferic {
namespace module {

// Constant per render session; frame_clock advances by block_size each block so
// a module always knows its absolute position on the global timeline.
struct ModuleContext {
  double sample_rate;     // Hz for this render session
  size_t block_size;      // frames per render_block call
  uint64_t frame_clock;   // absolute sample index at the start of the current block
};

// A self-contained, composable sound source (thunder, rain, wind, ocean, ...).
// The Soundscape compositor owns a heterogeneous list of these and drives them
// once per block. render_block() is ADDITIVE — a module sums its contribution
// into `mix`, never overwrites it, so multiple modules layer cleanly.
class SoundModule {
 public:
  virtual ~SoundModule() = default;

  // One-time setup before the first render_block (allocate voices, seed RNG).
  virtual void prepare(const ModuleContext& ctx) = 0;

  // Add this module's contribution for the current block into `mix`.
  virtual void render_block(AudioBuffer& mix, const ModuleContext& ctx) = 0;

  // Human-readable identifier, used in logs and routing.
  virtual std::string_view name() const = 0;
};

}  // namespace module
}  // namespace sferic
