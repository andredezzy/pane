// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/ground.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "pane/ui/design/text.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkSurfaceProps.h"
#include "ui/base/class_property.h"
#include "ui/color/color_provider.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(ui::ColorVariant*)

namespace pane {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(ui::ColorVariant, kGroundKey)

}  // namespace

void SetGround(views::View& surface, ui::ColorVariant ground) {
  const ui::ColorVariant* now = surface.GetProperty(kGroundKey);
  if (now && *now == ground) {
    return;
  }
  surface.SetProperty(kGroundKey, std::move(ground));
}

SkColor GroundOf(const views::View& view) {
  for (const views::View* up = &view; up; up = up->parent()) {
    if (const ui::ColorVariant* ground = up->GetProperty(kGroundKey)) {
      const ui::ColorProvider* colors = view.GetColorProvider();
      if (!colors && !ground->IsPhysical()) {
        return SK_ColorTRANSPARENT;
      }
      return ground->ResolveToSkColor(colors);
    }
  }
  return SK_ColorTRANSPARENT;
}

void RefreshLabelGrounds(views::View& view) {
  for (views::View* child : view.children()) {
    if (auto* label = views::AsViewClass<views::Label>(child)) {
      SetLabelGround(*label, GroundOf(*label));
    }
    RefreshLabelGrounds(*child);
  }
}

SkColor OverGround(SkColor fill, double opacity, SkColor ground) {
  // Blended as the raster blends it, by Skia itself, to the bit: a ground a
  // step off would show as a box around the text.
  SkBitmap pixel;
  pixel.allocN32Pixels(1, 1);
  pixel.eraseColor(ground);
  SkCanvas canvas(pixel, SkSurfaceProps());
  SkPaint paint;
  paint.setColor(fill);
  paint.setAlphaf(paint.getAlphaf() *
                  static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
  canvas.drawPaint(paint);
  return pixel.getColor(0, 0);
}

}  // namespace pane
