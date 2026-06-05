#include "module/rain/rain_module.h"

namespace sferic {
namespace rain {

RainModule::RainModule(double intensity_mm_h, uint32_t seed)
    : intensity_mm_h_(intensity_mm_h), seed_(seed) {}

void RainModule::prepare(const module::ModuleContext&) {}

void RainModule::render_block(AudioBuffer&, const module::ModuleContext&) {}

}  // namespace rain
}  // namespace sferic
