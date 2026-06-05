#pragma once

#include <memory>
#include <vector>

#include "module/module.h"

namespace sferic {
namespace ambience {

class Soundscape {
 public:
  void add_module(std::unique_ptr<module::SoundModule> m);
  void prepare(const module::ModuleContext& ctx);
  void render_block(AudioBuffer& mix, module::ModuleContext& ctx);

  size_t module_count() const { return modules_.size(); }

 private:
  std::vector<std::unique_ptr<module::SoundModule>> modules_;
};

}  // namespace ambience
}  // namespace sferic
