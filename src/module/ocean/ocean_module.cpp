#include "module/ocean/ocean_module.h"

namespace sferic {
namespace ocean {

OceanModule::OceanModule(double wave_height_m, double wave_period_s, uint32_t seed)
    : wave_height_m_(wave_height_m), wave_period_s_(wave_period_s), seed_(seed) {}

void OceanModule::prepare(const module::ModuleContext&) {}

void OceanModule::render_block(AudioBuffer&, const module::ModuleContext&) {}

}  // namespace ocean
}  // namespace sferic
