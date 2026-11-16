// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_SYSTEM_WORDS_H_
#define PANE_UI_DESIGN_SYSTEM_WORDS_H_

#include <string_view>

namespace pane {

// The words and keys Pane's own UI writes as each system writes them (Paper
// 11.6, 11.7): the Mac quits and writes keys as glyphs; Windows exits, as its
// apps and Chrome there do, and writes keys as words joined by +. What names
// the computer itself is the model's (HostDeviceName), and the menus' keys
// are the command table's (commands.h).
struct SystemWords {
  // The app's going: Quit Pane? and its button, the lock's update action
  // (Paper 04.5, 07.9).
  std::u16string_view quit;
  std::u16string_view quit_question;
  // The key on Quit Pane?'s button: ⌘Q on the Mac, Alt+F4 on Windows, which
  // its menu item shows too (11.5.1, 11.6).
  std::u16string_view quit_key;
  // The key on a dialog's default button (an extension's prompts, App icon's
  // Done): Return's glyph on the Mac, in its text form (VS15), since bare it
  // falls back to the emoji; Enter on Windows (11.6's A).
  std::u16string_view return_key;
  // What Settings says after "Version 0.15.0 is downloaded." where Pane
  // can't install it itself: the user drags it in on the Mac (06.3.1); on
  // Windows its installer opens as Pane exits (11.12.12, 11.12.13).
  std::u16string_view installs_by_hand;
  // The line above the pill, and Settings' row, after a swap that failed at
  // the last exit (11.12.14, the user's call E): Windows' alone, whose swap
  // runs once Pane has gone. The Mac's never shows: its swap runs before.
  std::u16string_view update_not_installed;
};

// This system's (system_words_mac.cc, system_words_win.cc).
const SystemWords& Words();

}  // namespace pane

#endif  // PANE_UI_DESIGN_SYSTEM_WORDS_H_
