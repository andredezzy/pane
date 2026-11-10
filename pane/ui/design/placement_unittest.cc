// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/placement.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view.h"

namespace pane {

namespace {

class PlacementTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  ui::AXPlatformForTest ax_platform_;
  views::View parent_;
  raw_ptr<views::View> view_ =
      parent_.AddChildView(std::make_unique<views::View>());
};

}  // namespace

// At 2x, a half point is a pixel: the bounds take the whole points, the layer
// the half, as SwiftUI puts a view centered on 278.5 at pixel 557.
TEST_F(PlacementTest, HalfPointsOnTwiceTheScale) {
  PlaceOnPixels(*view_, gfx::RectF(10.5f, 278.5f, 30, 40), 2);
  EXPECT_EQ(view_->bounds(), gfx::Rect(10, 278, 30, 40));
  EXPECT_EQ(view_->GetTransform(), gfx::Transform::MakeTranslation(0.5f, 0.5f));
}

// What falls between the screen's pixels rounds to the nearest one: a quarter
// point at 2x is half a pixel, rounded up, as SwiftUI rounds 599.5 to 600.
TEST_F(PlacementTest, RoundsToTheScreensPixels) {
  PlaceOnPixels(*view_, gfx::RectF(0, 20.25f, 30, 40), 2);
  EXPECT_EQ(view_->GetTransform(), gfx::Transform::MakeTranslation(0, 0.5f));
  PlaceOnPixels(*view_, gfx::RectF(0, 20.2f, 30, 40), 2);
  EXPECT_EQ(view_->GetTransform(), gfx::Transform());
  // At 1x, a pixel is a point.
  PlaceOnPixels(*view_, gfx::RectF(10.5f, 20.4f, 30, 40), 1);
  EXPECT_EQ(view_->GetTransform(), gfx::Transform::MakeTranslation(1, 0));
}

// On whole points it needs no layer.
TEST_F(PlacementTest, NoLayerOnWholePoints) {
  PlaceOnPixels(*view_, gfx::RectF(10, 20, 30, 40), 2);
  EXPECT_EQ(view_->bounds(), gfx::Rect(10, 20, 30, 40));
  EXPECT_FALSE(view_->layer());
}

}  // namespace pane
