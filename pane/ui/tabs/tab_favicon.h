// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TABS_TAB_FAVICON_H_
#define PANE_UI_TABS_TAB_FAVICON_H_

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
class ImageSkia;
class RectF;
}  // namespace gfx

namespace pane {

// What stands in for a tab's favicon while it has none: the sidebar's globe,
// or the switcher's square, the foreground at 10%.
enum class FaviconPlaceholder { kGlobe, kSquare };

// A tab's favicon in its 16-point place, rounded as a favicon is (ADR 0057),
// at `opacity`; without one, the placeholder in `color`. A place at a
// fraction of a point draws there, as SwiftUI draws one on its pixels: the
// switcher's rows sit on half points.
void PaintFavicon(gfx::Canvas& canvas,
                  const gfx::RectF& place,
                  const gfx::ImageSkia& favicon,
                  FaviconPlaceholder placeholder,
                  SkColor color,
                  float opacity = 1);

}  // namespace pane

#endif  // PANE_UI_TABS_TAB_FAVICON_H_
