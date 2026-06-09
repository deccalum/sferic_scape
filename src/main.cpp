#include "cli/browser.h"
#include "cli/commands.h"
#include "core/logger.h"
#include "io/env.h"

#define DEVELOPMENT 1

#if DEVELOPMENT

int main() {
  sferic::io::env::load();
  sferic::log::init(sferic::io::env::log_dir().string() + "/");

  sferic::cli::AnalyzeOptions opts;
  opts.emit_unhealed = true;
  opts.write_json = sferic::io::env::flag("SFERIC_WRITE_JSON");

  for (const auto& p : sferic::cli::browse(sferic::io::env::wav_root(), opts))
    sferic::cli::analyze(p, opts);

  return 0;
}

#else
static std::atomic<bool> g_running{true};

int main() {
  load_dotenv();
  log::init();
  controllerinterface::init();

  ambience::Soundscape scape;
  scape.add_module(std::make_unique<thunder::ThunderModule>());
  scape.add_module(std::make_unique<rain::RainModule>());
  scape.add_module(std::make_unique<wind::WindModule>());
  scape.add_module(std::make_unique<ocean::OceanModule>());

  io::RealtimeSink sink;
  module::ModuleContext ctx = sink.context();
  AudioBuffer mix(2, ctx.block_size, ctx.sample_rate);

  scape.prepare();
  std::signal();
  AudioBuffer mix();

  while (g_running.load()) {
    controllerinterface::poll(scape);
    scape.render_block(mix, ctx);
    limiter::process(mix);
    sink.write_block(mix);
    if (sink.xrun()) log_overload();
  }

  sink.finalize();
  return 0;
}

#endif
