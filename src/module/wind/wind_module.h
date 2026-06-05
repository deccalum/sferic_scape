#pragma once

#include <cstdint>
#include <string_view>

#include "module/module.h"

namespace sferic {
namespace wind {

class WindModule : public module::SoundModule {
 public:
  WindModule(double wind_speed_ms, uint32_t seed);

  void prepare(const module::ModuleContext& ctx) override;
  void render_block(AudioBuffer& mix, const module::ModuleContext& ctx) override;
  std::string_view name() const override { return "wind"; }

 private:
  double wind_speed_ms_;
  uint32_t seed_;
};

}  // namespace wind
}  // namespace sferic
