// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_EXTENSION_SHORTCUT_H_
#define PANE_MODEL_EXTENSION_SHORTCUT_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/enum_set.h"

namespace pane::model {

// An extension's keyboard shortcut (chrome.commands) as Chrome writes it,
// "Command+Shift+Y" (Command::AcceleratorToString), shown as the menu bar
// writes one: ⌃⌥⇧⌘, then the key. And Chrome's rules for a key typed into the
// Keyboard shortcuts dialog's field (cr_shortcut_input.ts, on macOS), with the
// engine's own (Command::StringToAccelerator) and Pane's: a key the menu bar
// holds stays Pane's (ADR 0098).

// The modifiers held with a key.
enum class KeyModifier { kCommand, kControl, kOption, kShift, kCapsLock };
using KeyModifiers =
    base::EnumSet<KeyModifier, KeyModifier::kCommand, KeyModifier::kCapsLock>;

// Why the field did not take a key; it keeps recording.
struct ShortcutRefusal {
  enum class Reason {
    // ⌥ with ⌃ or ⌘, or three modifiers, which Chrome's parse refuses.
    kInvalidCombination,
    // No ⌃, ⌥, or ⌘.
    kIncludeModifier,
    // Modifiers with a key that can't end a shortcut.
    kTypeALetter,
    // A key the menu bar holds.
    kPanes,
  };

  friend bool operator==(const ShortcutRefusal&,
                         const ShortcutRefusal&) = default;

  // Chrome's own macOS strings, and Pane's.
  std::string Message() const;

  Reason reason = Reason::kInvalidCombination;
  // For a key the menu bar holds: its item, and the key as Chrome writes it.
  std::string item;
  std::string shortcut;
};

// What the modifiers of a key down rule out, in Chrome's order, before its key
// is read: too many first, then none.
std::optional<ShortcutRefusal> ShortcutRefusalFor(KeyModifiers modifiers);

// "Command+Shift+Y" as ⇧⌘Y; empty stays empty.
std::string ShortcutGlyphs(std::string_view shortcut);

// Held modifiers, in the Mac's order.
std::string ModifierGlyphs(KeyModifiers modifiers);

}  // namespace pane::model

#endif  // PANE_MODEL_EXTENSION_SHORTCUT_H_
