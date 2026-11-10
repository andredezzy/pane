// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_TEXT_BUTTON_H_
#define PANE_UI_DESIGN_TEXT_BUTTON_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/text.h"
#include "ui/views/controls/button/button.h"

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
}  // namespace button_size

// A button with a title (the Swift's PaneButtonStyle): packages/ui's
// variants, its hover fading in 0.15 s with its title's tint, a press at once
// that fades as it lets go, 50% while disabled, and Pane's ring while it has
// the keyboard. An outline's 1-point border adds to its width, as CSS's
// border-box does.
class TextButton : public views::Button {
  METADATA_HEADER(TextButton, views::Button)

 public:
  TextButton(PressedCallback pressed,
             const std::u16string& title,
             ButtonVariant variant,
             const ButtonSize& size = button_size::kRegular);
  TextButton(const TextButton&) = delete;
  TextButton& operator=(const TextButton&) = delete;
  ~TextButton() override;

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

}  // namespace pane

#endif  // PANE_UI_DESIGN_TEXT_BUTTON_H_
