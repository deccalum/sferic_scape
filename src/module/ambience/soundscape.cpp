#include "module/ambience/soundscape.h"

#include <sstream>
#include <utility>

#include "core/logger.h"

namespace sferic {
namespace ambience {

void Soundscape::add_module(std::unique_ptr<module::SoundModule> m) {
  std::ostringstream ss;
  ss << "+ module: " << m->name();
  SFERIC_LOG(Info, ss.str());
  modules_.push_back(std::move(m));
}

void Soundscape::prepare(const module::ModuleContext& ctx) {
  SFERIC_SCOPE("Soundscape::prepare");
  for (auto& m : modules_) m->prepare(ctx);
}

void Soundscape::render_block(AudioBuffer& mix, module::ModuleContext& ctx) {
  const size_t n = ctx.block_size;
  for (size_t f = 0; f < n; ++f) mix.at(0, f) = 0.0f;
  for (auto& m : modules_) m->render_block(mix, ctx);
  ctx.frame_clock += n;
}

}  // namespace ambience
}  // namespace sferic
