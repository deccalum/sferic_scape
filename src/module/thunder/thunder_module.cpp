#include "module/thunder/thunder_module.h"

#include "core/logger.h"

namespace sferic {
namespace thunder {

ThunderModule::ThunderModule() { SFERIC_LOG(Info, "ThunderModule constructed"); }

void ThunderModule::prepare(const module::ModuleContext& ctx) {
  SFERIC_SCOPE("ThunderModule::prepare");
  (void)ctx;
  // TODO initialise voice pool and strike scheduler
}

void ThunderModule::render_block(AudioBuffer& mix, const module::ModuleContext& ctx) {
  // TODO advance storm clock, spawn voices for new strikes, mix active voices
  (void)mix;
  (void)ctx;
}

}  // namespace thunder
}  // namespace sferic
