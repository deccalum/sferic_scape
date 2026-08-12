#pragma once

#include <memory>
#include <string_view>

#include "analysis/detect.h"
#include "cli/args.h"
#include "cli/commands.h"
#include "core/domain.h"
#include "io/media_repository.h"
#include "ui/player.h"

namespace sferic::cli {

// The one place a concrete detector is chosen. Throws for a domain that has none.
std::unique_ptr<analysis::IDetector> make_detector(Domain domain);

void run(Command command, io::MediaRepository& repo, Domain domain, DetectOptions detect_opts = {});
void run_browse(std::string_view table);

// argv path — one parsed Invocation to its command. Returns the process exit code.
int dispatch(const Invocation& invocation, io::MediaRepository& repo,
             const ui::AudioSinkFactory& make_sink);

// No-argv path — the domain picker and the command menu loop.
void run_interactive(io::MediaRepository& repo, const ui::AudioSinkFactory& make_sink);

}  // namespace sferic::cli
