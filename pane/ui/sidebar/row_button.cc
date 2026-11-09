// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/row_button.h"

#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"

namespace pane {

RowButton::RowButton(PressedCallback pressed)
    : views::Button(std::move(pressed)), look_(RowLook(/*selected=*/false)) {
  highlight_ = std::make_unique<ControlHighlight>(
      this, look_.shape, look_.disabled_opacity,
      base::BindRepeating(&RowButton::LookChanged, base::Unretained(this)));
  auto background =
      std::make_unique<ControlBackground>(look_, highlight_.get());
  background_ = background.get();
  SetBackground(std::move(background));
  // The pointer over a tab's X is over its row: the row stays lit.
  SetNotifyEnterExitOnChild(true);
}

RowButton::~RowButton() = default;

void RowButton::SetSelected(bool selected) {
  if (selected != selected_) {
    selected_ = selected;
    UpdateLook();
  }
}

void RowButton::SetTintsOnHover(bool tints_on_hover) {
  if (tints_on_hover != tints_on_hover_) {
    tints_on_hover_ = tints_on_hover;
    UpdateLook();
  }
}

void RowButton::SetDimmed(bool dimmed) {
  if (dimmed != dimmed_) {
    dimmed_ = dimmed;
    LookChanged();
  }
}

void RowButton::SetLook(const ControlLook& look) {
  own_look_ = look;
  UpdateLook();
}

void RowButton::OnThemeChanged() {
  views::Button::OnThemeChanged();
  // Its colors, resolved again for the Theme.
  LookChanged();
}

double RowButton::hover() const {
  return highlight_->hover();
}

SkColor RowButton::Tint() const {
  const ui::ColorProvider* colors = GetColorProvider();
  const std::optional<SkColor> tint =
      colors ? TintOf(look_, *colors, hover()) : std::nullopt;
  if (!tint) {
    return SK_ColorTRANSPARENT;
  }
  return SkColorSetA(*tint, static_cast<U8CPU>(std::lround(SkColorGetA(*tint) *
                                                           ContentsOpacity())));
}

void RowButton::LookChanged() {
  SchedulePaint();
}

void RowButton::UpdateLook() {
  look_ = own_look_ ? *own_look_ : RowLook(selected_, tints_on_hover_);
  background_->SetLook(look_);
  LookChanged();
}

BEGIN_METADATA(RowButton)
END_METADATA

}  // namespace pane
