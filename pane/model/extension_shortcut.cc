// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/extension_shortcut.h"

#include <array>
#include <utility>

#include "base/containers/fixed_flat_map.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"

namespace pane::model {

namespace {

// Chrome's names for the keys that aren't a letter or a digit, as the menu bar
// draws them.
constexpr auto kKeyGlyphs =
    base::MakeFixedFlatMap<std::string_view, std::string_view>({
        {"Comma", ","},
        {"Period", "."},
        {"Up", "↑"},
        {"Down", "↓"},
        {"Left", "←"},
        {"Right", "→"},
        {"Insert", "Insert"},
        {"Delete", "⌦"},
        {"Home", "↖"},
        {"End", "↘"},
        {"PageUp", "⇞"},
        {"PageDown", "⇟"},
        {"Space", "Space"},
        {"Tab", "⇥"},
        {"MediaNextTrack", "⏭"},
        {"MediaPlayPause", "⏯"},
        {"MediaPrevTrack", "⏮"},
        {"MediaStop", "⏹"},
    });

constexpr std::array<std::pair<KeyModifier, std::string_view>, 4>
    kModifierGlyphs = {{
        {KeyModifier::kControl, "⌃"},
        {KeyModifier::kOption, "⌥"},
        {KeyModifier::kShift, "⇧"},
        {KeyModifier::kCommand, "⌘"},
    }};

}  // namespace

std::string ShortcutRefusal::Message() const {
  switch (reason) {
    case Reason::kInvalidCombination:
      return "Invalid combination";
    case Reason::kIncludeModifier:
      return "Include Ctrl, Alt, or ⌘";
    case Reason::kTypeALetter:
      return "Type a letter";
    case Reason::kPanes:
      return base::StrCat({ShortcutGlyphs(shortcut), " is Pane’s ", item});
  }
}

std::optional<ShortcutRefusal> ShortcutRefusalFor(KeyModifiers modifiers) {
  KeyModifiers held = modifiers;
  held.Remove(KeyModifier::kCapsLock);
  if (modifiers.Has(KeyModifier::kOption) &&
      modifiers.HasAny({KeyModifier::kControl, KeyModifier::kCommand})) {
    return ShortcutRefusal{.reason =
                               ShortcutRefusal::Reason::kInvalidCombination};
  }
  // ⌃⇧⌘: Chrome's field takes it, but its parse allows two modifiers and
  // would clear the shortcut instead.
  if (held.size() > 2) {
    return ShortcutRefusal{.reason =
                               ShortcutRefusal::Reason::kInvalidCombination};
  }
  if (!modifiers.HasAny({KeyModifier::kControl, KeyModifier::kOption,
                         KeyModifier::kCommand})) {
    return ShortcutRefusal{.reason = ShortcutRefusal::Reason::kIncludeModifier};
  }
  return std::nullopt;
}

std::string ShortcutGlyphs(std::string_view shortcut) {
  KeyModifiers modifiers;
  std::string_view key;
  for (std::string_view token : base::SplitStringPiece(
           shortcut, "+", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (token == "Ctrl") {
      modifiers.Put(KeyModifier::kControl);
    } else if (token == "Alt") {
      modifiers.Put(KeyModifier::kOption);
    } else if (token == "Shift") {
      modifiers.Put(KeyModifier::kShift);
    } else if (token == "Command") {
      modifiers.Put(KeyModifier::kCommand);
    } else {
      const auto glyph = kKeyGlyphs.find(token);
      key = glyph == kKeyGlyphs.end() ? token : glyph->second;
    }
  }
  return base::StrCat({ModifierGlyphs(modifiers), key});
}

std::string ModifierGlyphs(KeyModifiers modifiers) {
  std::string glyphs;
  for (const auto& [modifier, glyph] : kModifierGlyphs) {
    if (modifiers.Has(modifier)) {
      glyphs += glyph;
    }
  }
  return glyphs;
}

}  // namespace pane::model
