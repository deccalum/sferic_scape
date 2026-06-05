#pragma once

#include <cstdint>
#include <string_view>

#include "module/module.h"

namespace sferic {
namespace ocean {

class OceanModule : public module::SoundModule {
 public:
  OceanModule(double wave_height_m, double wave_period_s, uint32_t seed);

  void prepare(const module::ModuleContext& ctx) override;
  void render_block(AudioBuffer& mix, const module::ModuleContext& ctx) override;
  std::string_view name() const override { return "ocean"; }

 private:
  double wave_height_m_;
  double wave_period_s_;
  uint32_t seed_;
};

}  // namespace ocean
}  // namespace sferic
