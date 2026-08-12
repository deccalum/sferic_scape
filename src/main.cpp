#include <cstddef>
#include <memory>
#include <optional>
#include <qr/qr.hpp>

#include "cli/args.h"
#include "cli/browse.h"
#include "cli/dispatch.h"
#include "core/logger.h"
#include "io/audio_sink.h"
#include "io/env.h"
#include "io/media_repository.h"

int main(int argc, char** argv) {
  sferic::io::env::load();
  const std::optional<sferic::cli::Invocation> invocation = sferic::cli::parse_args(argc, argv);
  if (!invocation) return 1;
  qr::Connection db(sferic::io::env::database_url());
  qr::Connection log_db(sferic::io::env::database_url());
  sferic::log::init(log_db, sferic::log::LogConfig{});
  sferic::io::MediaRepository repo(db);
  sferic::cli::init_database_browser(db);
  const sferic::ui::AudioSinkFactory make_sink = [](size_t channels, double sample_rate) {
    return std::make_unique<sferic::io::RealtimeSink>(channels, sample_rate);
  };

  int exit_code = 0;
  if (invocation->spec) {
    exit_code = sferic::cli::dispatch(*invocation, repo, make_sink);
  } else {
    sferic::cli::run_interactive(repo, make_sink);
  }
  sferic::log::shutdown();
  return exit_code;
}
