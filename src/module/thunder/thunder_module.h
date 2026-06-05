#pragma once

#include <string_view>

#include "module/module.h"

namespace sferic {
namespace thunder {

// Procedural thunderstorm source. Drives a storm scheduler and maps each strike
// onto a ParametricModel synthesized by ParametricSynth. Inherits the generic
// SoundModule ABI so the Soundscape compositor can mix it with other sources.
class ThunderModule : public module::SoundModule {
 public:
  ThunderModule();

  void prepare(const module::ModuleContext& ctx) override;
  void render_block(AudioBuffer& mix, const module::ModuleContext& ctx) override;
  std::string_view name() const override { return "thunder"; }

 private:
  // TODO voice pool — one ParametricSynth per live strike
  // TODO strike scheduler (Storm, moving epicentre, Poisson timing)
};

}  // namespace thunder
}  // namespace sferic
