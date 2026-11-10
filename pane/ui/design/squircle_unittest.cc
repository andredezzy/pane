// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/squircle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "cc/paint/paint_flags.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPathMeasure.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"

namespace pane {

namespace {

// The design's scale, and the profile bars' 2 (ADR 0064).
constexpr float kRadii[] = {
    2,           radius::kXs,    radius::kSm, radius::kTab, radius::kMd,
    radius::kLg, radius::kPanel, radius::kXl, radius::k2xl};

// A fifth of a pixel at 2×, in points.
constexpr float kFifthOfAPixel = 0.1f;

// Squircle.swift's path, as the Swift app drew every corner: max(8, 4r)
// segments a corner, each point on |x|⁴ + |y|⁴ = 1 by the angle's cosine and
// sine to the half, clockwise from the top left corner's start.
std::vector<SkPoint> SwiftSquircle(const gfx::RectF& rect, float corner) {
  const float radius = std::min(std::max(corner, 0.0f),
                                std::min(rect.width(), rect.height()) / 2);
  struct Corner {
    float x;
    float y;
    float dx;
    float dy;
  };
  const std::array<Corner, 4> corners = {{
      {rect.x() + radius, rect.y() + radius, -1, -1},
      {rect.right() - radius, rect.y() + radius, 1, -1},
      {rect.right() - radius, rect.bottom() - radius, 1, 1},
      {rect.x() + radius, rect.bottom() - radius, -1, 1},
  }};
  const int steps = std::max(8, static_cast<int>(radius * 4));
  std::vector<SkPoint> points;
  for (int index = 0; index < 4; ++index) {
    const Corner& c = corners[index];
    for (int step = 0; step <= steps; ++step) {
      double angle = static_cast<double>(step) / steps * std::numbers::pi / 2;
      if (index % 2 == 1) {
        angle = std::numbers::pi / 2 - angle;
      }
      points.push_back(SkPoint::Make(
          c.x + c.dx * radius * std::pow(std::max(std::cos(angle), 0.0), 0.5),
          c.y + c.dy * radius * std::pow(std::max(std::sin(angle), 0.0), 0.5)));
    }
  }
  // Closed.
  points.push_back(points.front());
  return points;
}

// Points along the path's outline, `spacing` apart, closed.
std::vector<SkPoint> Sample(const SkPath& path, float spacing) {
  SkPathMeasure measure(path, /*forceClosed=*/true);
  const float length = measure.getLength();
  std::vector<SkPoint> points;
  for (float distance = 0; distance < length; distance += spacing) {
    SkPoint point;
    EXPECT_TRUE(measure.getPosTan(distance, &point, nullptr));
    points.push_back(point);
  }
  points.push_back(points.front());
  return points;
}

float DistanceToSegment(const SkPoint& p, const SkPoint& a, const SkPoint& b) {
  const SkVector ab = b - a;
  const float length2 = ab.dot(ab);
  const float t =
      length2 == 0 ? 0 : std::clamp((p - a).dot(ab) / length2, 0.0f, 1.0f);
  return SkPoint::Distance(p, a + ab * t);
}

float DistanceToPolyline(const SkPoint& p, const std::vector<SkPoint>& line) {
  float nearest = INFINITY;
  for (size_t i = 0; i + 1 < line.size(); ++i) {
    nearest = std::min(nearest, DistanceToSegment(p, line[i], line[i + 1]));
  }
  return nearest;
}

// How far apart two outlines are at their farthest (the Hausdorff distance).
float Apart(const std::vector<SkPoint>& a, const std::vector<SkPoint>& b) {
  float farthest = 0;
  for (const SkPoint& point : a) {
    farthest = std::max(farthest, DistanceToPolyline(point, b));
  }
  for (const SkPoint& point : b) {
    farthest = std::max(farthest, DistanceToPolyline(point, a));
  }
  return farthest;
}

}  // namespace

// Blink's two cubics a corner follow Squircle.swift's corners within a fifth
// of a pixel at every radius of the scale, on rectangles placed off the
// origin, wider than tall, and taller than wide.
TEST(SquircleTest, MatchesTheSwiftAppsSquircle) {
  for (const gfx::RectF& rect :
       {gfx::RectF(3.5f, 7, 120, 64), gfx::RectF(-10, 20.25f, 64, 120)}) {
    for (float radius : kRadii) {
      SCOPED_TRACE(testing::Message()
                   << "radius " << radius << " in " << rect.ToString());
      const float apart = Apart(Sample(SquirclePath(rect, radius), 0.05f),
                                SwiftSquircle(rect, radius));
      EXPECT_LT(apart, kFifthOfAPixel);
    }
  }
}

// A radius larger than half the shorter side shrinks to it, as the Swift's
// did: the profile's bar, 4 by 16 at 2, and a 28-point square asked for 29.
TEST(SquircleTest, ShrinksTheRadiusToHalfTheShorterSide) {
  for (const auto& [rect, radius] :
       {std::pair(gfx::RectF(0, 0, 4, 16), 2.0f),
        std::pair(gfx::RectF(10, 10, 28, 28), radius::k2xl),
        std::pair(gfx::RectF(0, 0, 36, 20), 18.0f)}) {
    SCOPED_TRACE(rect.ToString());
    EXPECT_LT(Apart(Sample(SquirclePath(rect, radius), 0.02f),
                    SwiftSquircle(rect, radius)),
              kFifthOfAPixel);
    // The curve meets each edge, and goes no further.
    const SkRect bounds = SquirclePath(rect, radius).computeTightBounds();
    EXPECT_NEAR(bounds.left(), rect.x(), 1e-3);
    EXPECT_NEAR(bounds.top(), rect.y(), 1e-3);
    EXPECT_NEAR(bounds.right(), rect.right(), 1e-3);
    EXPECT_NEAR(bounds.bottom(), rect.bottom(), 1e-3);
  }
}

// No radius, or no area, is the rectangle itself.
TEST(SquircleTest, NoRadiusIsTheRectangle) {
  SkRect rect;
  EXPECT_TRUE(SquirclePath(gfx::RectF(1, 2, 30, 40), 0).isRect(&rect));
  EXPECT_EQ(rect, SkRect::MakeXYWH(1, 2, 30, 40));
  // An empty one, its edges and no curve.
  const SkPath empty = SquirclePath(gfx::RectF(1, 2, 0, 40), 11);
  EXPECT_EQ(empty.getBounds(), SkRect::MakeXYWH(1, 2, 0, 40));
  EXPECT_EQ(empty.getSegmentMasks() & SkPath::kCubic_SegmentMask, 0u);
}

// The cached shape is the one a new one would be, wherever it is drawn.
TEST(SquircleTest, TheSameSizeIsTheSameShapeAnywhere) {
  const SkPath first = SquirclePath(gfx::RectF(0, 0, 80, 32), radius::kLg);
  const SkPath moved = SquirclePath(gfx::RectF(40, 12, 80, 32), radius::kLg);
  EXPECT_EQ(first.makeOffset(40, 12), moved);
  EXPECT_EQ(SquirclePath(gfx::RectF(0, 0, 80, 32), radius::kLg), first);
}

// A border's line runs half its width inside the edge, on a radius shrunk by
// as much, as SwiftUI's strokeBorder draws it: the field's 1-point border at
// 11 is the squircle of 10.5 inset by half a point.
TEST(SquircleTest, AStrokeRunsInsideTheEdge) {
  const gfx::RectF field(0, 0, 200, 36);
  gfx::RectF inset = field;
  inset.Inset(0.5f);
  EXPECT_EQ(SquircleStrokePath(field, radius::kMd, 1),
            SquirclePath(inset, radius::kMd - 0.5f));
}

namespace {

// The most any channel of two images of one size differs by.
int MostApart(const SkBitmap& a, const SkBitmap& b) {
  int most = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      const SkColor p = a.getColor(x, y);
      const SkColor q = b.getColor(x, y);
      for (const int shift : {0, 8, 16, 24}) {
        most = std::max(most, std::abs(static_cast<int>((p >> shift) & 0xFF) -
                                       static_cast<int>((q >> shift) & 0xFF)));
      }
    }
  }
  return most;
}

// What `paint` draws on a clear canvas `size` points big at `scale`.
template <typename Paint>
SkBitmap Painted(const gfx::Size& size, float scale, Paint paint) {
  gfx::Canvas canvas(size, scale, /*is_opaque=*/false);
  canvas.DrawColor(SK_ColorTRANSPARENT, SkBlendMode::kSrc);
  paint(canvas);
  return canvas.GetBitmap();
}

}  // namespace

// Settings' cards and a dialog's card and ring in pieces, so a tile inside
// them rasters no path: what the one path draws, at 1x and 2x, in colors that
// show a pixel covered twice; the border's corner caps within a 255th.
TEST(SquircleTest, PiecesDrawWhatThePathDraws) {
  const struct {
    gfx::RectF rect;
    float radius;
  } shapes[] = {
      // Settings' cards (a row, three, two), at their places.
      {gfx::RectF(40, 116, 640, 126), radius::kXl},
      {gfx::RectF(40, 298, 640, 182), radius::kXl},
      {gfx::RectF(40, 536, 640, 62), radius::kXl},
      // Quit Pane?'s and the App icon picker's cards in their layers.
      {gfx::RectF(1, 1, 480, 234), radius::k2xl},
      {gfx::RectF(1, 1, 480, 366), radius::k2xl},
      // A control's, under the size where pieces are drawn.
      {gfx::RectF(3, 7, 200, 36), radius::kMd},
  };
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColorSetARGB(0x80, 0x1C, 0x1C, 0x1F));
  cc::PaintFlags border;
  border.setAntiAlias(true);
  border.setStyle(cc::PaintFlags::kStroke_Style);
  border.setStrokeWidth(1);
  border.setColor(SkColorSetARGB(0x14, 0, 0, 0));
  for (const auto& [rect, radius] : shapes) {
    const gfx::Size size(rect.right() + 4, rect.bottom() + 4);
    for (const float scale : {1.0f, 2.0f}) {
      SCOPED_TRACE(testing::Message() << rect.ToString() << " r" << radius
                                      << " at " << scale << "x");
      EXPECT_EQ(0, MostApart(Painted(size, scale,
                                     [&](gfx::Canvas& canvas) {
                                       canvas.DrawPath(
                                           SquirclePath(rect, radius), fill);
                                     }),
                             Painted(size, scale, [&](gfx::Canvas& canvas) {
                               FillSquircle(canvas, rect, radius, fill);
                             })));
      // A ring around the card, as a dialog's: its radius a point more.
      for (const auto& [line, line_radius] :
           {std::pair(rect, radius),
            std::pair(gfx::RectF(rect.x() - 1, rect.y() - 1, rect.width() + 2,
                                 rect.height() + 2),
                      radius + 1)}) {
        EXPECT_LE(MostApart(Painted(size + gfx::Size(2, 2), scale,
                                    [&](gfx::Canvas& canvas) {
                                      canvas.DrawPath(SquircleStrokePath(
                                                          line, line_radius, 1),
                                                      border);
                                    }),
                            Painted(size + gfx::Size(2, 2), scale,
                                    [&](gfx::Canvas& canvas) {
                                      StrokeSquircleBorder(
                                          canvas, line, line_radius, 1, border);
                                    })),
                  1);
      }
    }
  }
}

}  // namespace pane
