// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/icon_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace pane {

IconButton::IconButton(PressedCallback pressed,
                       int size,
                       const Glyph& glyph,
                       const std::u16string& name,
                       const ControlLook& look)
    : views::Button(std::move(pressed)),
      size_(size),
      glyph_(glyph),
      look_(look) {
  highlight_ = std::make_unique<ControlHighlight>(
      this, look_.shape, look_.disabled_opacity,
      base::BindRepeating(&IconButton::SchedulePaint, base::Unretained(this)));
  auto background =
      std::make_unique<ControlBackground>(look_, highlight_.get());
  background_ = background.get();
  SetBackground(std::move(background));
  SetName(name);
}

IconButton::~IconButton() = default;

void IconButton::SetGlyph(const Glyph& glyph) {
  glyph_ = glyph;
  SchedulePaint();
}

void IconButton::SetName(const std::u16string& name) {
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
}

void IconButton::SetLook(const ControlLook& look) {
  look_ = look;
  background_->SetLook(look);
  SchedulePaint();
}

void IconButton::SetHeld(bool held) {
  // What it opened had the pointer's events: it hears where the pointer is
  // now.
  if (!held && GetState() != STATE_DISABLED) {
    SetState(IsMouseHovered() ? STATE_HOVERED : STATE_NORMAL);
  }
  highlight_->SetHeld(held);
}

gfx::Size IconButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(size_, size_);
}

void IconButton::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  const std::optional<SkColor> tint =
      colors ? TintOf(look_, *colors, highlight_->hover()) : std::nullopt;
  if (!tint) {
    return;
  }
  PaintIcon(
      *canvas, glyph_.icon,
      gfx::RectF((width() - glyph_.size) / 2.0f,
                 (height() - glyph_.size) / 2.0f, glyph_.size, glyph_.size),
      glyph_.stroke_width, *tint);
}

BEGIN_METADATA(IconButton)
END_METADATA

}  // namespace pane
