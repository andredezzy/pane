// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/icons.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"

namespace pane {

namespace {

constexpr Icon kIcons[] = {
    Icon::kArrowLeft,
    Icon::kChevronDown,
    Icon::kChevronLeft,
    Icon::kChevronRight,
    Icon::kChevronUp,
    Icon::kChevronsDownUp,
    Icon::kChevronsUpDown,
    Icon::kCircleDashedX,
    Icon::kGlobe,
    Icon::kMonitor,
    Icon::kMoon,
    Icon::kPencil,
    Icon::kRotateCw,
    Icon::kSettings,
    Icon::kSquareArrowOutUpRight,
    Icon::kSun,
    Icon::kTimerOff,
    Icon::kTrash2,
    Icon::kTriangleAlert,
    Icon::kX,
    Icon::kDownload,
    Icon::kEllipsis,
    Icon::kPin,
    Icon::kPinOff,
    Icon::kPuzzle,
};

std::vector<SkPoint> PointsOf(const SkPath& path) {
  std::vector<SkPoint> points(path.countPoints());
  path.getPoints(points);
  return points;
}

}  // namespace

// Every icon's SVG, as the Swift app's, parses, and stays on Lucide's
// 24-unit grid.
TEST(IconsTest, EveryIconParsesOnTheGrid) {
  for (Icon icon : kIcons) {
    SCOPED_TRACE(static_cast<int>(icon));
    const std::optional<std::vector<IconShape>> shapes =
        ParseIconElements(IconElements(icon));
    ASSERT_TRUE(shapes);
    ASSERT_FALSE(shapes->empty());
    for (const IconShape& shape : *shapes) {
      EXPECT_FALSE(shape.path.isEmpty());
      const SkRect bounds = shape.path.computeTightBounds();
      EXPECT_GE(bounds.left(), 0);
      EXPECT_GE(bounds.top(), 0);
      EXPECT_LE(bounds.right(), 24);
      EXPECT_LE(bounds.bottom(), 24);
    }
  }
}

// A path's commands land where SVG puts them: the chevron's relative lines,
// and the X's two strokes.
TEST(IconsTest, PathsAreParsedAsSvgDrawsThem) {
  const std::vector<IconShape>& chevron = IconShapes(Icon::kChevronDown);
  ASSERT_EQ(chevron.size(), 1u);
  EXPECT_EQ(PointsOf(chevron[0].path),
            (std::vector<SkPoint>{{6, 9}, {12, 15}, {18, 9}}));

  const std::vector<IconShape>& x = IconShapes(Icon::kX);
  ASSERT_EQ(x.size(), 2u);
  EXPECT_EQ(PointsOf(x[0].path), (std::vector<SkPoint>{{18, 6}, {6, 18}}));
  EXPECT_EQ(PointsOf(x[1].path), (std::vector<SkPoint>{{6, 6}, {18, 18}}));
  EXPECT_FALSE(x[0].filled);
  EXPECT_FALSE(x[0].dash);
}

// Circles and rectangles are Skia's own shapes: the ellipsis's dots filled,
// the dashed circle's dash kept, the monitor's rectangle rounded at 2.
TEST(IconsTest, CirclesAndRectsKeepTheirAttributes) {
  const std::vector<IconShape>& dots = IconShapes(Icon::kEllipsis);
  ASSERT_EQ(dots.size(), 3u);
  for (const IconShape& dot : dots) {
    EXPECT_TRUE(dot.filled);
  }
  SkRect oval;
  EXPECT_TRUE(dots[0].path.isOval(&oval));
  EXPECT_FLOAT_EQ(oval.left(), 3.4f - 1.9f);
  EXPECT_FLOAT_EQ(oval.width(), 3.8f);

  const std::vector<IconShape>& dashed = IconShapes(Icon::kCircleDashedX);
  ASSERT_EQ(dashed.size(), 3u);
  ASSERT_TRUE(dashed[0].dash);
  EXPECT_FLOAT_EQ(dashed[0].dash->first, 2.6f);
  EXPECT_FLOAT_EQ(dashed[0].dash->second, 2.6f);
  EXPECT_FALSE(dashed[0].filled);

  SkRRect rect;
  ASSERT_TRUE(IconShapes(Icon::kMonitor)[0].path.isRRect(&rect));
  EXPECT_EQ(rect.rect(), SkRect::MakeXYWH(2, 3, 20, 14));
  EXPECT_EQ(rect.getSimpleRadii(), SkVector::Make(2, 2));
}

// What no icon of Pane's uses is refused rather than drawn wrong.
TEST(IconsTest, RefusesWhatItDoesNotDraw) {
  EXPECT_FALSE(ParseIconElements(R"(<polyline points="1 2 3 4"/>)"));
  EXPECT_FALSE(ParseIconElements(R"(<path d="M1 1" transform="x"/>)"));
  EXPECT_FALSE(ParseIconElements(R"(<path d="M1 1 Q"/>)"));
  EXPECT_FALSE(ParseIconElements(R"(<circle cx="1" cy="1"/>)"));
  EXPECT_FALSE(ParseIconElements(R"(<path d="M1 1")"));
  EXPECT_FALSE(ParseIconElements(""));
}

// Drawn, the back arrow's shaft (M19 12H5) is its stroke at the width asked
// for, scaled from the grid: at 24 points and a stroke of 2, the shaft's
// middle is the color, a pixel past its stroke's edge is clear, and the
// corners are clear.
TEST(IconsTest, PaintsStrokesAtTheWidthAskedFor) {
  gfx::Canvas canvas(gfx::Size(24, 24), /*image_scale=*/1, /*is_opaque=*/false);
  canvas.DrawColor(SK_ColorTRANSPARENT);
  PaintIcon(canvas, Icon::kArrowLeft, gfx::RectF(0, 0, 24, 24), 2, SK_ColorRED);
  const SkBitmap bitmap = canvas.GetBitmap();
  EXPECT_EQ(bitmap.getColor(12, 12), SK_ColorRED);
  EXPECT_EQ(bitmap.getColor(16, 12), SK_ColorRED);
  EXPECT_EQ(SkColorGetA(bitmap.getColor(16, 14)), 0u);
  EXPECT_EQ(SkColorGetA(bitmap.getColor(1, 1)), 0u);

  // At 12 points the grid's 4 is 2 points: the shaft covers its pixels
  // whole, and its round cap ends a point past the line's end, and no more.
  gfx::Canvas small(gfx::Size(12, 12), 1, false);
  small.DrawColor(SK_ColorTRANSPARENT);
  PaintIcon(small, Icon::kArrowLeft, gfx::RectF(0, 0, 12, 12), 4, SK_ColorRED);
  EXPECT_EQ(small.GetBitmap().getColor(6, 6), SK_ColorRED);
  EXPECT_EQ(small.GetBitmap().getColor(5, 5), SK_ColorRED);
  EXPECT_GT(SkColorGetA(small.GetBitmap().getColor(10, 6)), 0u);
  EXPECT_EQ(SkColorGetA(small.GetBitmap().getColor(11, 6)), 0u);
  EXPECT_EQ(SkColorGetA(small.GetBitmap().getColor(8, 8)), 0u);
}

}  // namespace pane
