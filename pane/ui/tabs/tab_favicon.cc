// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/tabs/tab_favicon.h"

#include <cmath>

#include "cc/paint/paint_flags.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/squircle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/scoped_canvas.h"

namespace pane {

void PaintFavicon(gfx::Canvas& canvas,
                  const gfx::RectF& place,
                  const gfx::ImageSkia& favicon,
                  FaviconPlaceholder placeholder,
                  SkColor color,
                  float opacity) {
  if (favicon.isNull() && placeholder == FaviconPlaceholder::kGlobe) {
    PaintIcon(canvas, Icon::kGlobe, place, 2, color);
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  if (favicon.isNull()) {
    flags.setColor(color);
    canvas.DrawPath(SquirclePath(place, radius::kXs), flags);
    return;
  }
  gfx::ScopedCanvas scoped(&canvas);
  // At the place's own origin, a fraction of a point included.
  canvas.sk_canvas()->translate(place.x(), place.y());
  const gfx::RectF box(place.size());
  canvas.ClipPath(SquirclePath(box, radius::kXs), true);
  flags.setAlphaf(opacity);
  canvas.DrawImageInt(favicon, 0, 0, favicon.width(), favicon.height(), 0, 0,
                      std::round(box.width()), std::round(box.height()),
                      /*filter=*/true, flags);
}

}  // namespace pane
