#include "cli/args.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

namespace sferic::cli {

const CommandSpec* find_command(std::string_view name) {
  const auto it = std::ranges::find(kCommands, name, &CommandSpec::name);
  return it == kCommands.end() ? nullptr : &*it;
}

void print_usage(std::ostream& os) {
  size_t width = 0;
  for (const CommandSpec& spec : kCommands)
    width = std::max(width, spec.name.size() + spec.arg.size() + 1);

  os << "sferic — physics-based soundscape synthesizer\n\n"
     << "usage: sferic [command] [arg] [flags…]   (no command launches the interactive menu)\n\n"
     << "commands:\n";
  for (const CommandSpec& spec : kCommands) {
    const std::string left = std::string(spec.name) + ' ' + std::string(spec.arg);
    os << "  " << left << std::string(width - left.size() + 2, ' ') << spec.help << '\n';
  }
  os << "\ndetect flags (optional after <domain>):\n"
     << "  --curated    walk the curated corpus instead of reference (default)\n"
     << "  --reference  walk the reference corpus (the default; explicit form)\n"
     << "  --force      re-run even if media_detection already has name@version stamps —\n"
     << "               drops every exemplar row for those media, verdicts included\n"
     << "  --carry      carry approved/rejected verdicts onto matching new windows (default)\n"
     << "  --no-carry   re-judge from scratch — queue every new window for review\n"
     << "\nextract flags (optional after <domain>):\n"
     << "  --force      rebuild — clear the domain's samples and re-extract all\n"
     << "               (default is incremental: samples already in the library are skipped)\n"
     << "\nreview sets (optional flag after <domain>; default --pending):\n"
     << "  --pending    unreviewed candidates (approved IS NULL)\n"
     << "  --approved   previously approved exemplars (re-listen / demote)\n"
     << "  --rejected   previously rejected exemplars (re-listen / promote)\n";
}

namespace {

std::optional<DetectOptions> parse_detect_opts(int argc, char** argv, int start) {
  DetectOptions opts;
  std::optional<bool> carry;
  for (int i = start; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--force" || arg == "force") {
      opts.force = true;
    } else if (arg == "--carry" || arg == "carry") {
      carry = true;
    } else if (arg == "--no-carry" || arg == "no-carry") {
      carry = false;
    } else if (arg == "--curated" || arg == "curated") {
      opts.corpus = CorpusRole::Curated;
    } else if (arg == "--reference" || arg == "reference") {
      opts.corpus = CorpusRole::Reference;
    } else {
      std::cerr << "unknown detect flag: " << arg << "\n\n";
      return std::nullopt;
    }
  }
  if (carry) opts.carry_verdicts = *carry;
  return opts;
}

std::optional<ExtractOptions> parse_extract_opts(int argc, char** argv, int start) {
  ExtractOptions opts;
  for (int i = start; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--force" || arg == "force") {
      opts.force = true;
    } else {
      std::cerr << "unknown extract flag: " << arg << "\n\n";
      return std::nullopt;
    }
  }
  return opts;
}

std::optional<ReviewSet> parse_review_set(int argc, char** argv, int start) {
  std::optional<ReviewSet> set;
  for (int i = start; i < argc; ++i) {
    const std::string_view arg = argv[i];
    ReviewSet next;
    if (arg == "--pending" || arg == "pending")
      next = ReviewSet::Pending;
    else if (arg == "--approved" || arg == "approved")
      next = ReviewSet::Approved;
    else if (arg == "--rejected" || arg == "rejected")
      next = ReviewSet::Rejected;
    else {
      std::cerr << "unknown review flag: " << arg << "\n\n";
      return std::nullopt;
    }
    if (set) {
      std::cerr << "review accepts at most one of --pending/--approved/--rejected\n\n";
      return std::nullopt;
    }
    set = next;
  }
  return set.value_or(ReviewSet::Pending);
}

}  // namespace

std::optional<Invocation> parse_args(int argc, char** argv) {
  Invocation inv;
  if (argc < 2) return inv;
  inv.spec = find_command(argv[1]);
  if (!inv.spec) {
    std::cerr << "unknown command: " << argv[1] << "\n\n";
    print_usage(std::cerr);
    return std::nullopt;
  }
  if (!inv.spec->arg.empty() && argc < 3) {
    std::cerr << "command '" << inv.spec->name << "' needs an argument " << inv.spec->arg << "\n\n";
    print_usage(std::cerr);
    return std::nullopt;
  }
  if (argc >= 3) {
    if (inv.spec->command == Command::Browse) {
      inv.table = argv[2];
    } else {
      inv.domain = try_from_string(argv[2], std::type_identity<Domain>{});
      if (!inv.domain) {
        std::cerr << "unknown domain: " << argv[2] << "\n\n";
        print_usage(std::cerr);
        return std::nullopt;
      }
    }
  }
  if (inv.spec->command == Command::Review) {
    const std::optional<ReviewSet> parsed = parse_review_set(argc, argv, 3);
    if (!parsed) {
      print_usage(std::cerr);
      return std::nullopt;
    }
    inv.review_set = *parsed;
  } else if (inv.spec->command == Command::Detect) {
    const std::optional<DetectOptions> parsed = parse_detect_opts(argc, argv, 3);
    if (!parsed) {
      print_usage(std::cerr);
      return std::nullopt;
    }
    inv.detect_opts = *parsed;
  } else if (inv.spec->command == Command::Extract) {
    const std::optional<ExtractOptions> parsed = parse_extract_opts(argc, argv, 3);
    if (!parsed) {
      print_usage(std::cerr);
      return std::nullopt;
    }
    inv.extract_opts = *parsed;
  } else if (argc > 3) {
    std::cerr << "unexpected extra argument: " << argv[3] << "\n\n";
    print_usage(std::cerr);
    return std::nullopt;
  }
  return inv;
}

}  // namespace sferic::cli
