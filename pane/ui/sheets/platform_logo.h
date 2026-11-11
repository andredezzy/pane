// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SHEETS_PLATFORM_LOGO_H_
#define PANE_UI_SHEETS_PLATFORM_LOGO_H_

#include "pane/model/profile.h"
#include "ui/gfx/geometry/size_f.h"

namespace gfx {
class Canvas;
class PointF;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

namespace pane {

// Each platform's own logo, in its real colors, at the size Paper draws it
// on the profile sheet's tiles (the Swift's PlatformLogo, from the assets it
// drew): Windows' four squares at 22 points, Apple's at 21.13 by 26 in the
// foreground's one color, Tux at 22.56 by 26, edged in light on dark so its
// black reads there. Drawn from their own vector paths, so they stay sharp at
// every scale.
gfx::SizeF PlatformLogoSize(model::Platform platform);

// Draws the logo with its top left at `origin`, in `colors`' appearance.
void PaintPlatformLogo(gfx::Canvas& canvas,
                       const ui::ColorProvider& colors,
                       model::Platform platform,
                       const gfx::PointF& origin);

}  // namespace pane

#endif  // PANE_UI_SHEETS_PLATFORM_LOGO_H_
