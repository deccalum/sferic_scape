#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "analysis/spectral_envelope.h"
#include "core/buffer.h"
#include "core/logger.h"
#include "io/audio_file.h"
#include "io/parametric_json.h"
#include "synthesis/noise_generator.h"
#include "synthesis/parametric_synth.h"

#define DEVELOPMENT 1

namespace fs = std::filesystem;
using namespace sferic;

static void load_dotenv(const fs::path& path = ".env") {
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    ::setenv(key.c_str(), val.c_str(), 0);
  }
}

#if DEVELOPMENT
int main() {
  load_dotenv();
  log::init();

  const fs::path input = std::getenv("SFERIC_INPUT");
  const fs::path out_dir = std::getenv("SFERIC_OUT_DIR");

  AudioBuffer audio = io::load(input.string());
  analysis::SpectralEnvelope spectral = analysis::SpectralAnalyzer(2048, 0, 15).analyze(audio);
  analysis::ParametricModel model = analysis::ParametricExtractor().extract(spectral);

  const fs::path group = out_dir / input.stem();
  fs::create_directories(group);
  io::save_parametric_json(model, (group / "model.full.json").string(), io::TrajectoryMode::Full);
  io::save_parametric_json(model, (group / "model.compact.json").string(),
                           io::TrajectoryMode::Compact);

  {
    synthesis::NoiseGenerator gen;
    gen.load_model(spectral);
    const size_t n = static_cast<size_t>(spectral.duration() * spectral.sample_rate);
    AudioBuffer spectral_out(1, n, spectral.sample_rate);
    gen.render(spectral_out.channel(0), n, 0.0);
    io::save(spectral_out, (group / "spectral.wav").string());
  }

  synthesis::ParametricSynth synth;
  synth.load_model(model);
  AudioBuffer out = synth.synthesize();
  io::save(out, (group / "synth.wav").string());

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
