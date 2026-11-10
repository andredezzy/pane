// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/placement.h"

#include <cmath>

#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view.h"

namespace pane {

void PlaceOnPixels(views::View& view, const gfx::RectF& bounds, float scale) {
  const float x = std::floor(bounds.x());
  const float y = std::floor(bounds.y());
  view.SetBounds(x, y, std::round(bounds.width()), std::round(bounds.height()));
  // What the whole points leave, on the screen's pixels.
  const auto rest = [&](float place, float whole) {
    return std::round((place - whole) * scale) / scale;
  };
  const gfx::Transform shift =
      gfx::Transform::MakeTranslation(rest(bounds.x(), x), rest(bounds.y(), y));
  if (shift.IsIdentity() && !view.layer()) {
    return;
  }
  if (!view.layer()) {
    view.SetPaintToLayer();
    view.layer()->SetFillsBoundsOpaquely(false);
  }
  view.SetTransform(shift);
}

}  // namespace pane
