// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/anchored_menu.h"

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace pane {

gfx::Point MenuAnchorUnder(const gfx::Rect& trigger) {
  return trigger.bottom_left();
}

}  // namespace pane
