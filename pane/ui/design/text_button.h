// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_TEXT_BUTTON_H_
#define PANE_UI_DESIGN_TEXT_BUTTON_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/text.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
class RectF;
}

namespace views {
class Label;
}

namespace pane {

// A button's height, its padding at either end, and its title's text
// (Buttons.swift's ButtonSize).
struct ButtonSize {
  int height;
  int leading;
  int trailing;
  TextStyle text;
};

namespace button_size {
// h-9 px-4 text-sm.
inline constexpr ButtonSize kRegular{36, 16, 16, text::kButton};
// h-8 px-3 text-xs: Settings' PIN and update buttons.
inline constexpr ButtonSize kSmall{32, 12, 12, text::kButtonSmall};
// A page's dialog's (DialogButtonStyle): 28 points, 10 in, at 13.
inline constexpr ButtonSize kAlert{28, 10, 10, {13, weight::kRegular, 16}};
// Aside's popups': h-8, 14 in, at 13 (an extension's Remove).
inline constexpr ButtonSize kPopup{32, 14, 14, text::kButtonPopup};
// h-7, 10 in, at 12: a form's secondary action (the profile sheet's Test
// connection).
inline constexpr ButtonSize kCompact{28, 10, 10, text::kButtonSmall};
}  // namespace button_size

// A button with a title (the Swift's PaneButtonStyle): packages/ui's
// variants, its hover fading in 0.15 s with its title's tint, a press at once
// that fades as it lets go, 50% while disabled, and Pane's ring while it has
// the keyboard. An outline's 1-point border adds to its width, as CSS's
// border-box does, and so does a popup's Cancel's.
class TextButton : public views::Button {
  METADATA_HEADER(TextButton, views::Button)

 public:
  TextButton(PressedCallback pressed,
             const std::u16string& title,
             ButtonVariant variant,
             const ButtonSize& size = button_size::kRegular);
  // In a look of its own: a variant's at another radius (the profile sheet's
  // Test connection, an outline rounded 9). Its border, when it has one,
  // adds to its width.
  TextButton(PressedCallback pressed,
             const std::u16string& title,
             const ControlLook& look,
             const ButtonSize& size);
  TextButton(const TextButton&) = delete;
  TextButton& operator=(const TextButton&) = delete;
  ~TextButton() override;

  // Another title, as Check for updates turns to Checking…; its width
  // follows.
  void SetTitle(const std::u16string& title);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  // The title's tint, as lit as the hover is.
  void Tint();

  const ControlLook look_;
  const ButtonSize size_;
  // The outline's border, at either end.
  const int border_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> title_ = nullptr;
};

// A dialog's button with the key that presses it on a chip beside its title
// (the Swift's KeyedLabel in its `.keyed` style): 36 points, the title 14 in
// at 14 medium, then 8, then the key at 12 medium on a chip 20 tall, 6 in on
// either side and rounded 9, 8 from the end; rounded 15. Its role picks the
// look (KeyedLabel.Role): Cancel's, on the card with a hairline, the accent
// under the pointer, its key muted on the secondary chip (Quit Pane?'s Esc);
// the destructive one's, filled, its key white on a faint white chip (⌘Q);
// the primary's, filled, its key faint on a faint chip (the App icon
// picker's Done, ↩). The title keeps its color under the pointer.
class KeyedButton : public views::Button {
  METADATA_HEADER(KeyedButton, views::Button)

 public:
  enum class Role { kCancel, kDestructive, kPrimary };

  KeyedButton(PressedCallback pressed,
              const std::u16string& title,
              const std::u16string& key,
              Role role = Role::kPrimary);
  KeyedButton(const KeyedButton&) = delete;
  KeyedButton& operator=(const KeyedButton&) = delete;
  ~KeyedButton() override;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  // The chip, around the key's label.
  gfx::RectF ChipBounds() const;

  const Role role_;
  const ControlLook look_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> key_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_TEXT_BUTTON_H_
