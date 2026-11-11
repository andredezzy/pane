// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/checkbox.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "pane/ui/design/focus_ring.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// From the box's frame to its label, as AppKit sets a checkbox's title: the
// label's first glyph 6 points after the frame.
constexpr int kLabelGap = 5;

}  // namespace

Checkbox::Checkbox(const std::u16string& label,
                   const TextStyle& style,
                   ui::ColorVariant label_color,
                   Toggled toggled)
    : views::Button(
          base::BindRepeating(&Checkbox::Pressed, base::Unretained(this))),
      toggled_(std::move(toggled)) {
  label_ = AddChildView(MakeLabel(label, style, label_color));
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_->SetCanProcessEventsWithinSubtree(false);
  // No fill and no hover of its own: the box shows its press, and the ring
  // goes around the box, as the system's does.
  ControlLook look(ControlShape::Squircle(radius::kXs));
  highlight_ = std::make_unique<ControlHighlight>(
      this, look.shape, look.disabled_opacity,
      base::BindRepeating(&Checkbox::SchedulePaint, base::Unretained(this)));
  highlight_->ring()->SetAround(
      base::BindRepeating([](const views::View* view) {
        return static_cast<const Checkbox*>(view)->BoxBounds();
      }));
  GetViewAccessibility().SetRole(ax::mojom::Role::kCheckBox);
  GetViewAccessibility().SetName(label);
  UpdateAccessibility();
}

Checkbox::~Checkbox() = default;

void Checkbox::SetChecked(bool checked) {
  if (checked_ == checked) {
    return;
  }
  checked_ = checked;
  UpdateAccessibility();
  SchedulePaint();
}

gfx::Rect Checkbox::BoxBounds() const {
  return gfx::Rect(0, (height() - kSystemCheckboxSize) / 2, kSystemCheckboxSize,
                   kSystemCheckboxSize);
}

gfx::Size Checkbox::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const gfx::Size label = label_->GetPreferredSize({});
  return gfx::Size(kSystemCheckboxSize + kLabelGap + label.width(),
                   std::max(kSystemCheckboxSize, label.height()));
}

void Checkbox::Layout(PassKey) {
  const int x = kSystemCheckboxSize + kLabelGap;
  const int label_height = label_->GetPreferredSize({}).height();
  label_->SetBounds(x, (height() - label_height) / 2, std::max(width() - x, 0),
                    label_height);
}

void Checkbox::OnThemeChanged() {
  views::Button::OnThemeChanged();
  SchedulePaint();
}

void Checkbox::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::NativeTheme* theme = GetNativeTheme();
  PaintSystemCheckbox(
      *canvas, GetColorProvider(), BoxBounds(),
      {.checked = checked_,
       .pressed = GetState() == STATE_PRESSED,
       .enabled = GetEnabled(),
       .dark = theme && theme->preferred_color_scheme() ==
                            ui::NativeTheme::PreferredColorScheme::kDark});
}

void Checkbox::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  // The press shows at once, and goes as it lets go, as the system's box.
  SchedulePaint();
}

void Checkbox::Pressed() {
  checked_ = !checked_;
  UpdateAccessibility();
  SchedulePaint();
  if (toggled_) {
    toggled_.Run(checked_);
  }
}

void Checkbox::UpdateAccessibility() {
  GetViewAccessibility().SetCheckedState(checked_
                                             ? ax::mojom::CheckedState::kTrue
                                             : ax::mojom::CheckedState::kFalse);
}

BEGIN_METADATA(Checkbox)
END_METADATA

}  // namespace pane
