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
                       const std::u16string& name)
    : views::Button(std::move(pressed)),
      size_(size),
      glyph_(glyph),
      look_(IconButtonLook()) {
  highlight_ = std::make_unique<ControlHighlight>(
      this, look_.shape, look_.disabled_opacity,
      base::BindRepeating(&IconButton::SchedulePaint, base::Unretained(this)));
  SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
}

IconButton::~IconButton() = default;

void IconButton::SetGlyph(const Glyph& glyph) {
  glyph_ = glyph;
  SchedulePaint();
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
