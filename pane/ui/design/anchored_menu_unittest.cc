// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/anchored_menu.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace pane {

// The profile sheet's time zone trigger as a real click found it (108 by 32,
// its top left at 1129, 538 of the screen): its menu's top left at the
// trigger's bottom left, wherever the pointer pressed it.
TEST(AnchoredMenuTest, AMenuShowsJustUnderItsTriggerOnItsLeadingEdge) {
  EXPECT_EQ(MenuAnchorUnder(gfx::Rect(1129, 538, 108, 32)),
            gfx::Point(1129, 570));
  EXPECT_EQ(MenuAnchorUnder(gfx::Rect(-1440, 12, 160, 32)),
            gfx::Point(-1440, 44));
}

}  // namespace pane
