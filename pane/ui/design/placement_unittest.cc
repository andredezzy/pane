// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/placement.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/point_f.h"
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

// A drag's place in a list, where its event went to a row of it sliding on
// its layer, 12.15 points down, under the pointer at 131 in the window (the
// list at 39, the row at 43 in it). Views carries the event into the row on
// whole points, 36 where it is 36.85, which back in the list would be 91.15
// for 92, the copy of a dragged profile that much short (ADR 0128); from the
// event's place in the window it is 92, whatever the slide's moment.
TEST_F(PlacementTest, PointInARowSlidingAFraction) {
  view_->SetBounds(0, 39, 260, 676);
  views::View* row = view_->AddChildView(std::make_unique<views::View>());
  row->SetBounds(8, 43, 244, 36);
  row->SetPaintToLayer();
  row->SetTransform(gfx::Transform::MakeTranslation(0, 12.15f));
  const ui::MouseEvent in_window(ui::EventType::kMouseDragged,
                                 gfx::PointF(100, 131), gfx::PointF(100, 131),
                                 base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                                 ui::EF_LEFT_MOUSE_BUTTON);
  // As the root view hands it to the pressed row.
  const ui::MouseEvent in_row(in_window, static_cast<views::View*>(&parent_),
                              row);
  EXPECT_EQ(in_row.location_f(), gfx::PointF(92, 36));
  EXPECT_EQ(PointIn(*view_, in_row), gfx::PointF(100, 92));
  EXPECT_EQ(PointIn(parent_, in_row), gfx::PointF(100, 131));
}

}  // namespace pane
