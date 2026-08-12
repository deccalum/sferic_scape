#include "ui/term.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>

namespace sferic::ui {
namespace {

size_t visible_rows() {
  winsize ws{};
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  return ws.ws_row > 6 ? static_cast<size_t>(ws.ws_row) - 6 : 8;
}

void render(std::string_view title, std::span<const std::string> items, size_t cursor,
            size_t scroll, size_t rows) {
  std::cout << "\033[2J\033[H";
  std::cout << "\033[1m" << title << "\033[0m\n\n";
  const size_t end = std::min(scroll + rows, items.size());
  for (size_t i = scroll; i < end; ++i) {
    const bool is_cur = (i == cursor);
    if (is_cur) std::cout << "\033[7m";
    std::cout << (is_cur ? " › " : "   ") << items[i];
    if (is_cur) std::cout << "\033[0m";
    std::cout << "\n";
  }
  std::cout << "\n\033[90m↑↓/jk: move  ENTER: select  Q/ESQ: back";
  if (items.size() > rows) std::cout << "   " << cursor + 1 << "/" << items.size();
  std::cout << "\033[0m\n";
  std::cout.flush();
}

}  // namespace

RawMode::RawMode() {
  tcgetattr(STDIN_FILENO, &orig_);
  termios raw = orig_;
  raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | ISIG);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

RawMode::~RawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_); }

int read_key() {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) <= 0) return 27;
  if (c != 27) return static_cast<int>(c);
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  timeval tv{0, 50000};
  if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) return 27;
  unsigned char seq[2];
  if (read(STDIN_FILENO, &seq[0], 1) <= 0 || seq[0] != '[') return 27;
  if (read(STDIN_FILENO, &seq[1], 1) <= 0) return 27;
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
  return 0;
}

std::optional<size_t> select(std::string_view title, std::span<const std::string> items) {
  if (items.empty()) return std::nullopt;
  RawMode raw_mode;
  size_t cursor = 0;
  size_t scroll = 0;
  while (true) {
    const size_t rows = visible_rows();
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + rows) scroll = cursor - rows + 1;
    render(title, items, cursor, scroll, rows);
    switch (read_key()) {
      case 'q':
      case 27:  // Esc
      case 3:   // Ctrl+C — ISIG is disabled so this arrives as byte 3
        std::cout << "\033[2J\033[H";
        return std::nullopt;
      case '\r':
      case '\n':
        std::cout << "\033[2J\033[H";
        return cursor;
      case Key::Up:
      case 'k':
        if (cursor > 0) --cursor;
        break;
      case Key::Down:
      case 'j':
        if (cursor + 1 < items.size()) ++cursor;
        break;
    }
  }
}

void wait_for_key(std::string_view prompt) {
  std::cout << prompt;
  std::cout.flush();
  RawMode raw_mode;
  read_key();
}

}  // namespace sferic::ui
