// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/input.h"

#include <Carbon/Carbon.h>

#include <algorithm>
#include <map>

#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "pane/automation/native_input_mac.h"

namespace pane {

namespace {

// The keys scenarios type, by the US layout's virtual codes.
const std::map<std::string, uint16_t>& KeyCodes() {
  static const base::NoDestructor<std::map<std::string, uint16_t>> codes({
      {"a", kVK_ANSI_A},
      {"f", kVK_ANSI_F},
      {"g", kVK_ANSI_G},
      {"i", kVK_ANSI_I},
      {"k", kVK_ANSI_K},
      {"l", kVK_ANSI_L},
      {"p", kVK_ANSI_P},
      {"q", kVK_ANSI_Q},
      {"r", kVK_ANSI_R},
      {"t", kVK_ANSI_T},
      {"u", kVK_ANSI_U},
      {"w", kVK_ANSI_W},
      {"y", kVK_ANSI_Y},
      {"0", kVK_ANSI_0},
      {"1", kVK_ANSI_1},
      {"2", kVK_ANSI_2},
      {"3", kVK_ANSI_3},
      {"4", kVK_ANSI_4},
      {"5", kVK_ANSI_5},
      {"6", kVK_ANSI_6},
      {"7", kVK_ANSI_7},
      {"8", kVK_ANSI_8},
      {"9", kVK_ANSI_9},
      {"=", kVK_ANSI_Equal},
      {"-", kVK_ANSI_Minus},
      {"[", kVK_ANSI_LeftBracket},
      {"]", kVK_ANSI_RightBracket},
      {",", kVK_ANSI_Comma},
      {"\t", kVK_Tab},
      {"\r", kVK_Return},
      {"\x1b", kVK_Escape},
      // Delete, back a character, as the key types it (NSDeleteCharacter).
      {"\x7f", kVK_Delete},
  });
  return *codes;
}

CGEventFlags Flags(const std::vector<std::string>& modifiers) {
  uint64_t flags = 0;
  for (const std::string& name : modifiers) {
    if (name == "command") {
      flags |= kCGEventFlagMaskCommand;
    } else if (name == "shift") {
      flags |= kCGEventFlagMaskShift;
    } else if (name == "control") {
      flags |= kCGEventFlagMaskControl;
    } else if (name == "option") {
      flags |= kCGEventFlagMaskAlternate;
    } else if (name == "capsLock") {
      flags |= kCGEventFlagMaskAlphaShift;
    }
  }
  return static_cast<CGEventFlags>(flags);
}

// The key a modifier is, as a flagsChanged event names it.
uint16_t ModifierKeyCode(const std::string& modifier) {
  if (modifier == "command") {
    return kVK_Command;
  }
  if (modifier == "shift") {
    return kVK_Shift;
  }
  if (modifier == "option") {
    return kVK_Option;
  }
  if (modifier == "capsLock") {
    return kVK_CapsLock;
  }
  return kVK_Control;
}

// A key down then up: to the app's queue, its characters as typed (Shift
// makes a letter uppercase) both ways, or to the window, its characters
// ignoring the modifiers as on the key.
bool Type(views::Widget* widget,
          const std::string& key,
          const std::vector<std::string>& modifiers,
          bool to_window) {
  const auto code = KeyCodes().find(key);
  if (code == KeyCodes().end()) {
    return false;
  }
  const CGEventFlags flags = Flags(modifiers);
  if (flags && !to_window) {
    PostModifiers(widget, modifiers);
  }
  const std::string characters =
      (flags & kCGEventFlagMaskShift) ? base::ToUpperASCII(key) : key;
  for (const bool down : {true, false}) {
    native_input::Key(widget, down, flags, characters,
                      to_window ? key : characters, code->second, to_window);
  }
  return true;
}

}  // namespace

bool PostKey(views::Widget* widget,
             const std::string& key,
             const std::vector<std::string>& modifiers) {
  return Type(widget, key, modifiers, /*to_window=*/false);
}

bool SendKeyToWindow(views::Widget* widget,
                     const std::string& key,
                     const std::vector<std::string>& modifiers) {
  return Type(widget, key, modifiers, /*to_window=*/true);
}

void PostModifiers(views::Widget* widget,
                   const std::vector<std::string>& modifiers) {
  // What the channel holds down, as fingers would: a key posted with
  // modifiers leaves them down (a Ctrl-Tab's Ctrl), and releaseModifiers
  // lets each go, the last pressed first.
  static base::NoDestructor<std::vector<std::string>> held;
  // Each change names its own key, as a real one does: Chromium drops a
  // flagsChanged that names none (keyCode 0, crbug.com/889618), so a release
  // that named none never reached the page or Views.
  const auto post = [widget](const std::string& modifier) {
    native_input::FlagsChanged(widget, Flags(*held), ModifierKeyCode(modifier));
  };
  for (size_t i = held->size(); i-- > 0;) {
    if (!std::ranges::contains(modifiers, (*held)[i])) {
      const std::string modifier = (*held)[i];
      held->erase(held->begin() + i);
      post(modifier);
    }
  }
  for (const std::string& modifier : modifiers) {
    if (!std::ranges::contains(*held, modifier)) {
      held->push_back(modifier);
      post(modifier);
    }
  }
}

}  // namespace pane
