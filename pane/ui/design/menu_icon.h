// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_MENU_ICON_H_
#define PANE_UI_DESIGN_MENU_ICON_H_

#include "pane/ui/design/icons.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

// A menu item's icon (the Swift's MenuLabel): Lucide's at 16 points, its lines
// 1.5 of the grid, as the Electron app's menus drew them, in black, at each
// scale.
gfx::ImageSkia MenuIconImage(Icon icon);

// The icon as a menu shows it, in the menu's own text color, highlighted or
// not: on the Mac a template image, which an NSMenu tints itself.
ui::ImageModel MenuIcon(Icon icon);

}  // namespace pane

#endif  // PANE_UI_DESIGN_MENU_ICON_H_
