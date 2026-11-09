// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/sidebar_button.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

constexpr TextStyle kPlus{18, weight::kLight, 0};

}  // namespace

void PaintPlusMark(gfx::Canvas& canvas,
                   const gfx::RectF& place,
                   SkColor color) {
  // Centered on the place to the fraction of a point, as SwiftUI centers the
  // text's line in its frame, which the glyph's line overflows; Views lays
  // text out on whole points, which put it half a point left of and under the
  // Swift's (measured against its render).
  const gfx::FontList& font = SystemFont(kPlus.size, kPlus.weight);
  const float width = gfx::GetStringWidthF(u"+", font);
  const int height = font.GetHeight();
  const gfx::PointF origin(place.CenterPoint().x() - width / 2,
                           place.CenterPoint().y() - height / 2.0f - 0.5f);
  const gfx::Point whole(std::floor(origin.x()), std::floor(origin.y()));
  gfx::ScopedCanvas scoped(&canvas);
  canvas.sk_canvas()->translate(origin.x() - whole.x(), origin.y() - whole.y());
  canvas.DrawStringRectWithFlags(
      u"+", font, color,
      gfx::Rect(whole, gfx::Size(std::ceil(width) + 1, height)),
      gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

SidebarButton::SidebarButton(PressedCallback pressed,
                             Mark mark,
                             const std::u16string& title)
    : RowButton(std::move(pressed)), mark_(mark) {
  title_ =
      AddChildView(MakeLabel(title, text::kSidebar, kPaneColorMutedForeground));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetName(title);
  LookChanged();
}

SidebarButton::~SidebarButton() = default;

gfx::Size SidebarButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kHeight);
}

void SidebarButton::Layout(PassKey) {
  const int start = kLeading + kIconSize + kIconSpacing;
  title_->SetBounds(start, 0, std::max(width() - start - kTrailing, 0),
                    height());
}

void SidebarButton::PaintButtonContents(gfx::Canvas* canvas) {
  const gfx::RectF place(kLeading, (height() - kIconSize) / 2.0f, kIconSize,
                         kIconSize);
  const SkColor tint = Tint();
  switch (mark_) {
    case Mark::kPlus:
      PaintPlusMark(*canvas, place, tint);
      break;
    case Mark::kSettings:
      PaintIcon(*canvas, Icon::kSettings, place, 2, tint);
      break;
  }
}

void SidebarButton::LookChanged() {
  if (GetColorProvider()) {
    title_->SetEnabledColor(Tint());
  }
  RowButton::LookChanged();
}

BEGIN_METADATA(SidebarButton)
END_METADATA

}  // namespace pane
