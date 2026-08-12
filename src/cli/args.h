#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string_view>

#include "cli/commands.h"

namespace sferic::cli {

// closed set of verbs this executable exposes
enum class Command : uint8_t { Ingest, Detect, Extract, Analyze, Review, Browse, Clear };

// source of truth for argv parsing and the usage listing.
struct CommandSpec {
  std::string_view name;  // argv verb token ("detect", "review", …)
  Command command;        // typed verb this row dispatches to
  std::string_view arg;   // the positional argument the verb takes
  std::string_view help;  // one-line description
};

inline constexpr std::array kCommands{
    CommandSpec{
        "ingest", Command::Ingest, "<domain>",
        "pull curated freesound recordings for <domain> into the library (runs the Python ingest)"},
    CommandSpec{"detect", Command::Detect, "<domain> [--curated] [--force] [--carry|--no-carry]",
                "run the transient detector on undetected reference (or --curated) media"},
    CommandSpec{"extract", Command::Extract, "<domain> [--force]",
                "extract approved exemplars into samples (incremental; --force rebuilds)"},
    CommandSpec{"analyze", Command::Analyze, "<domain>",
                "score the resynthesis of every curated sample (interactive: pick which)"},
    CommandSpec{"review", Command::Review, "<domain> [--pending|--approved|--rejected]",
                "play exemplar candidates and approve/reject them (default: pending)"},
    CommandSpec{"browse", Command::Browse, "<table>", "dump one table to stdout"},
    CommandSpec{"clear", Command::Clear, "<domain>",
                "delete all pending (unreviewed) exemplars for <domain> — approved/rejected rows "
                "untouched"},
};

const CommandSpec* find_command(std::string_view name);
void print_usage(std::ostream& os);

// typed form of argv resolved once at the entry point.
struct Invocation {
  const CommandSpec* spec = nullptr;          // matched kCommands row (null = bare / menu)
  std::optional<Domain> domain;               // resolved positional — every command but browse
  std::string_view table;                     // browse only — key into the table registry
  ReviewSet review_set = ReviewSet::Pending;  // --pending / --approved / --rejected
  DetectOptions detect_opts{};                // --curated / --force / carry knobs
  ExtractOptions extract_opts{};              // --force rebuild
};

// Parses argv into a typed Invocation before any DB connection is opened.
std::optional<Invocation> parse_args(int argc, char** argv);

}  // namespace sferic::cli
