#include "cli/browser.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace sferic::cli {
namespace {

struct RawMode {
  termios orig_;
  RawMode() {
    tcgetattr(STDIN_FILENO, &orig_);
    termios raw = orig_;
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }
  ~RawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_); }
};

struct Entry {
  fs::path path;
  bool is_dir;
};

int terminal_rows() {
  struct winsize ws{};
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  return ws.ws_row > 6 ? static_cast<int>(ws.ws_row) - 6 : 8;
}

enum Key : int { Up = 300, Down, Right, Left };

int read_key() {
  unsigned char c;
  read(STDIN_FILENO, &c, 1);
  if (c != 27) return static_cast<int>(c);
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  timeval tv{0, 50000};
  if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) return 27;

  unsigned char seq[2];
  read(STDIN_FILENO, &seq[0], 1);
  if (seq[0] != '[') return 27;
  read(STDIN_FILENO, &seq[1], 1);

  switch (seq[1]) {
    case 'A':
      return Key::Up;
    case 'B':
      return Key::Down;
    case 'C':
      return Key::Right;
    case 'D':
      return Key::Left;
  }
  return 27;
}

std::vector<Entry> list_dir(const fs::path& dir) {
  std::vector<Entry> dirs, files;
  for (const auto& de : fs::directory_iterator(dir)) {
    if (de.is_directory())
      dirs.push_back({de.path(), true});
    else if (de.path().extension() == ".wav")
      files.push_back({de.path(), false});
  }
  const auto by_name = [](const Entry& a, const Entry& b) {
    return a.path.filename() < b.path.filename();
  };
  std::sort(dirs.begin(), dirs.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  dirs.insert(dirs.end(), files.begin(), files.end());
  return dirs;
}

void render(const fs::path& current, const fs::path& root, const std::vector<Entry>& entries,
            const std::set<fs::path>& selected, int cursor, int scroll, bool write_json) {
  const int rows = terminal_rows();
  std::cout << "\033[2J\033[H";
  std::cout << fs::relative(current, root.parent_path()).string() << "\n";
  std::cout << "\033[2m.json output:\033[0m "
            << (write_json ? "\033[1mon\033[0m" : "\033[2moff\033[0m") << "\n\n";

  const int end = std::min<int>(scroll + rows, static_cast<int>(entries.size()));
  for (int i = scroll; i < end; ++i) {
    const bool is_sel = selected.count(entries[i].path) > 0;
    const bool is_cur = (i == cursor);
    if (is_cur) std::cout << "\033[7m";
    std::cout << "  " << (is_sel ? "[x] " : "[ ] ") << entries[i].path.filename().string();
    if (entries[i].is_dir) std::cout << "/";
    if (is_cur) std::cout << "\033[0m";
    std::cout << "\n";
  }

  std::cout
      << "\n\033[2m"
      << "↑↓: move  space: toggle  →/enter: open dir  ←: back  a: all wav  c: confirm  j: toggle .json out  q: quit"
      << "\033[0m\n";
  std::cout.flush();
}

}  // namespace

std::vector<fs::path> browse(const fs::path& root, AnalyzeOptions& opts) {
  RawMode raw_mode;
  fs::path current = root;
  auto entries = list_dir(current);
  std::set<fs::path> selected;
  int cursor = 0;
  int scroll = 0;

  while (true) {
    render(current, root, entries, selected, cursor, scroll, opts.write_json);

    const int rows = terminal_rows();
    const int n = static_cast<int>(entries.size());
    const int k = read_key();

    switch (k) {
      case 'q':
      case 3:  // Ctrl+C — ISIG is disabled so this arrives as byte 3
        std::cout << "\033[2J\033[H";
        return {};

      case 'c':
        std::cout << "\033[2J\033[H";
        return {selected.begin(), selected.end()};

      case Key::Up:
        if (cursor > 0) {
          --cursor;
          if (cursor < scroll) scroll = cursor;
        }
        break;

      case Key::Down:
        if (cursor + 1 < n) {
          ++cursor;
          if (cursor >= scroll + rows) scroll = cursor - rows + 1;
        }
        break;

      case ' ':
        if (n > 0) {
          const fs::path& p = entries[cursor].path;
          if (selected.count(p))
            selected.erase(p);
          else
            selected.insert(p);
        }
        break;

      case 'j':
        opts.write_json = !opts.write_json;
        break;

      case 'a': {
        const bool any_wav = std::any_of(entries.begin(), entries.end(), [&](const Entry& e) {
          return !e.is_dir && selected.count(e.path);
        });
        for (const auto& e : entries) {
          if (e.is_dir) continue;
          if (any_wav)
            selected.erase(e.path);
          else
            selected.insert(e.path);
        }
        break;
      }

      case Key::Right:
      case '\r':
      case '\n':
        if (n > 0 && entries[cursor].is_dir) {
          current = entries[cursor].path;
          entries = list_dir(current);
          cursor = 0;
          scroll = 0;
        }
        break;

      case Key::Left:
      case 127:  // Backspace
        if (current != root) {
          current = current.parent_path();
          entries = list_dir(current);
          cursor = 0;
          scroll = 0;
        }
        break;
    }
  }
}

}  // namespace sferic::cli
