// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_SCREEN_H_
#define PANE_INSTALLER_SCREEN_H_

#include <optional>
#include <string>
#include <vector>

namespace pane::installer {

// What the install window shows in one state (Paper 11.12.6 to 11.12.10):
// the App icon, a title, a message, then a bar, a box, or buttons.
struct Screen {
  // Pane's keyed buttons (11.13 A): outlined with Esc, filled with Enter,
  // or red with no key, never the default.
  enum class ButtonKind { kOutline, kPrimary, kDestructive };

  struct Button {
    int id = 0;
    std::u16string label;
    ButtonKind kind = ButtonKind::kOutline;
    // "Esc", "Enter", or nothing.
    std::u16string key;
  };

  // The bar's labels; how full it is comes from what the window is told at
  // each frame.
  struct Bar {
    std::u16string left;
    // Its right label, in tabular figures: "62%", "1.3 of 3.4 GB".
    std::u16string right;
  };

  // The uninstall's box: what it says, ticked or not.
  struct Box {
    bool ticked = false;
    std::u16string label;
    // The data's size, beside the label; empty while it is counted.
    std::u16string size;
    std::u16string line;
  };

  std::u16string title;
  std::u16string message;
  std::optional<Bar> bar;
  std::optional<Box> box;
  std::vector<Button> buttons;
  // The button Enter presses when none has the focus, and Esc's: 0 for none.
  int enter = 0;
  int escape = 0;
};

}  // namespace pane::installer

#endif  // PANE_INSTALLER_SCREEN_H_
