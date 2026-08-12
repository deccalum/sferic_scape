#pragma once

#include <termios.h>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/logger.h"

namespace sferic::ui {

enum Key : int { Up = 300, Down, Right, Left };  // outside the byte range read_key returns

// RAII STDIN raw mode — echo, canonical mode and signal generation off.
struct RawMode {
  termios orig_;  // restored on destruction
  RawMode();
  ~RawMode();
  RawMode(const RawMode&) = delete;
  RawMode& operator=(const RawMode&) = delete;
};

// One key press, arrow escape sequences resolved to Key. Esc on anything else.
int read_key();

// Full-screen cursor menu. Returns the chosen index, or nullopt on q/Esc/Ctrl+C.
std::optional<size_t> select(std::string_view title, std::span<const std::string> items);

void wait_for_key(std::string_view prompt);

// Silences the logger's stdout echo while a full-screen view owns the terminal.
struct MuteLogEcho {
  bool prev_;  // echo state to restore
  MuteLogEcho() : prev_(log::echo_stdout()) { log::set_echo_stdout(false); }
  ~MuteLogEcho() { log::set_echo_stdout(prev_); }
  MuteLogEcho(const MuteLogEcho&) = delete;
  MuteLogEcho& operator=(const MuteLogEcho&) = delete;
};

// An extra key binding a browse_playable view offers beyond replay/info/back.
template <typename Row>
struct RowAction {
  int key;                              // keypress that fires this action
  std::string hint;                     // e.g. "[a]nalyze" — shown in the status bar
  std::function<void(const Row&)> run;  // called with the selected row
};

// select() over `rows`, play the chosen one, then hold on it for replay/info/
// extra actions until the user goes back. `label` renders a row as a menu line.
template <typename Row, typename Label, typename Play, typename Info>
void browse_playable(std::string_view title, const std::vector<Row>& rows, Label label,
                     Play play_row, Info info_row, const std::vector<RowAction<Row>>& extra = {}) {
  if (rows.empty()) {
    std::printf("  nothing here yet\n");
    std::fflush(stdout);
    wait_for_key("\n\033[90mpress any key to go back\033[0m");
    return;
  }

  std::string bar = "[space]replay [i]nfo ";
  for (const RowAction<Row>& a : extra) bar += a.hint + " ";
  bar += "[q]back\n";

  while (true) {
    std::vector<std::string> items;
    items.reserve(rows.size());
    for (const Row& r : rows) items.push_back(label(r));
    const std::optional<size_t> choice = select(title, items);
    if (!choice) return;
    const Row& r = rows[*choice];
    play_row(r);
    RawMode raw;
    bool back = false;
    while (!back) {
      std::printf("%s", bar.c_str());
      std::fflush(stdout);
      const int k = read_key();
      if (k == ' ') {
        play_row(r);
      } else if (k == 'i') {
        info_row(r);
      } else if (k == 'q' || k == 27 || k == Key::Left) {
        back = true;
      } else {
        for (const RowAction<Row>& a : extra)
          if (k == a.key) {
            a.run(r);
            break;
          }
      }
    }
  }
}

}  // namespace sferic::ui
