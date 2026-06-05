#include "module/wind/wind_module.h"

namespace sferic {
namespace wind {

WindModule::WindModule(double wind_speed_ms, uint32_t seed)
    : wind_speed_ms_(wind_speed_ms), seed_(seed) {}

void WindModule::prepare(const module::ModuleContext&) {}

void WindModule::render_block(AudioBuffer&, const module::ModuleContext&) {}

}  // namespace wind
}  // namespace sferic
