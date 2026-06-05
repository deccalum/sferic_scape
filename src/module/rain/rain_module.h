#pragma once

#include <cstdint>
#include <string_view>

#include "module/module.h"

namespace sferic {
namespace rain {

class RainModule : public module::SoundModule {
 public:
  RainModule(double intensity_mm_h, uint32_t seed);

  void prepare(const module::ModuleContext& ctx) override;
  void render_block(AudioBuffer& mix, const module::ModuleContext& ctx) override;
  std::string_view name() const override { return "rain"; }

 private:
  double intensity_mm_h_;
  uint32_t seed_;
};

}  // namespace rain
}  // namespace sferic
