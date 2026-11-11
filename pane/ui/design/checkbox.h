// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_CHECKBOX_H_
#define PANE_UI_DESIGN_CHECKBOX_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/text.h"
#include "ui/color/color_variant.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

namespace views {
class Label;
}

namespace pane {

// The platform's own checkbox: what the system draws stays as the system
// draws it (ADR 0058), off or on, pressed or not, in the light or the dark
// appearance. Its frame is kSystemCheckboxSize square, the box inset in it
// as the system insets it.
struct SystemCheckboxState {
  bool checked = false;
  bool pressed = false;
  bool enabled = true;
  bool dark = false;
};
inline constexpr int kSystemCheckboxSize = 16;
// The box drawn into `bounds`, a frame kSystemCheckboxSize square, at the
// canvas's scale: AppKit's own cell on the Mac, the native theme's box (its
// colors from `colors`) on Windows.
void PaintSystemCheckbox(gfx::Canvas& canvas,
                         const ui::ColorProvider* colors,
                         const gfx::Rect& bounds,
                         const SystemCheckboxState& state);

// A checkbox with its label after it, as SwiftUI's .toggleStyle(.checkbox)
// drew one (an extension's Report abuse, ADR 0056): the system's box, then
// the label, the two centered on one line. A click on the box or its label
// toggles it, as does Space with the keyboard on it; a press shows the box
// pressed at once. VoiceOver reads a checkbox named by its label, checked or
// not.
class Checkbox : public views::Button {
  METADATA_HEADER(Checkbox, views::Button)

 public:
  using Toggled = base::RepeatingCallback<void(bool checked)>;

  Checkbox(const std::u16string& label,
           const TextStyle& style,
           ui::ColorVariant label_color,
           Toggled toggled = Toggled());
  Checkbox(const Checkbox&) = delete;
  Checkbox& operator=(const Checkbox&) = delete;
  ~Checkbox() override;

  bool checked() const { return checked_; }
  // From the model: no `toggled`.
  void SetChecked(bool checked);
  // The box, in this view.
  gfx::Rect BoxBounds() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void StateChanged(ButtonState old_state) override;

 private:
  void Pressed();
  void UpdateAccessibility();

  Toggled toggled_;
  bool checked_ = false;
  raw_ptr<views::Label> label_ = nullptr;
  // Pane's ring around the box while it has the keyboard.
  std::unique_ptr<ControlHighlight> highlight_;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_CHECKBOX_H_
