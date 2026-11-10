// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/squircle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <tuple>

#include "base/containers/lru_cache.h"
#include "base/no_destructor.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace pane {

namespace {

// |x|⁴ + |y|⁴ = 1: a superellipse's exponent, which Blink calls a corner's
// curvature (2 is a circle), and CSS's superellipse(2).
constexpr float kCurvature = 4;

// Squircles kept by size and radius: a window's few dozen shapes, and the
// steps of one morphing.
constexpr size_t kCachedPaths = 64;

// Half a corner, from its start on the first edge to its middle (45°), as one
// cubic in the corner's own box: the start at (0, 1), the far corner at
// (1, 1). Blink's fit for a convex superellipse, by its curvature
// (third_party/blink/renderer/platform/geometry/path_builder.cc,
// ApproximateSuperellipseHalfCornerAsBezierCurve; BSD-licensed, Copyright
// 2025 The Chromium Authors): two control points and the end.
std::array<gfx::Vector2dF, 3> HalfCorner(float curvature) {
  static constexpr std::array<double, 7> p = {
      1.2430920942724248, 2.010479023614843,  0.32922901179443753,
      0.2823023142212073, 1.3473704261055421, 2.9149468637949814,
      0.9106507102917086};
  const float s = std::log2(curvature);
  const float slope =
      p[0] + (p[6] - p[0]) * 0.5 * (1 + std::tanh(p[5] * (s - p[1])));
  const float base = 1 / (1 + std::exp(-slope * (0 - p[1])));
  const float logistic = 1 / (1 + std::exp(-slope * (s - p[1])));
  const float a = (logistic - base) / (1 - base);
  const float b = p[2] * std::exp(-p[3] * std::pow(s, p[4]));
  // The superellipse's own point at 45°.
  const float half = std::pow(0.5f, 1 / curvature);
  return {gfx::Vector2dF(a, 1), gfx::Vector2dF(half - b, half + b),
          gfx::Vector2dF(half, half)};
}

// One corner, from its start on one edge to its end on the next, clockwise:
// `outer` is the rectangle's corner, `center` the radius box's inner one.
void AddCorner(SkPathBuilder& path,
               const gfx::PointF& start,
               const gfx::PointF& outer,
               const gfx::PointF& end,
               const gfx::PointF& center) {
  static const std::array<gfx::Vector2dF, 3> kHalf = HalfCorner(kCurvature);
  // A point of the corner's box: x toward the outer corner from the start's
  // side, y toward the start from the center (Blink's Corner::MapPoint).
  const gfx::Vector2dF along = outer - start;
  const gfx::Vector2dF across = start - center;
  const auto map = [&](const gfx::Vector2dF& point) {
    const gfx::PointF mapped = center + gfx::ScaleVector2d(along, point.x()) +
                               gfx::ScaleVector2d(across, point.y());
    return SkPoint::Make(mapped.x(), mapped.y());
  };
  const auto transposed = [](const gfx::Vector2dF& point) {
    return gfx::Vector2dF(point.y(), point.x());
  };
  path.lineTo(start.x(), start.y());
  path.cubicTo(map(kHalf[0]), map(kHalf[1]), map(kHalf[2]));
  path.cubicTo(map(transposed(kHalf[1])), map(transposed(kHalf[0])),
               SkPoint::Make(end.x(), end.y()));
}

// The squircle `width` by `height` at the origin, its radius already within
// half the shorter side.
SkPath MakeSquircle(float width, float height, float r) {
  SkPathBuilder path;
  path.moveTo(0, r);
  AddCorner(path, {0, r}, {0, 0}, {r, 0}, {r, r});
  AddCorner(path, {width - r, 0}, {width, 0}, {width, r}, {width - r, r});
  AddCorner(path, {width, height - r}, {width, height}, {width - r, height},
            {width - r, height - r});
  AddCorner(path, {r, height}, {0, height}, {0, height - r}, {r, height - r});
  path.close();
  return path.detach();
}

// The corner box's side, in pixels, past which Skia fills a corner's path
// the way it fills the whole path's: up to 32 pixels wide, it accumulates a
// small path's coverage in a mask (SkScan_AAAPath.cpp, MaskAdditiveBlitter),
// which differs by up to 4/255 along the curve.
constexpr float kPieceMinimumPixels = 33;

// Whether pieces of the squircle `rect`, its corners' boxes `side` square,
// meet on whole pixels on `canvas`, which draws them as the whole path does:
// whole points at a whole scale, and boxes big enough.
bool DrawsInPieces(const gfx::Canvas& canvas,
                   const gfx::RectF& rect,
                   float side) {
  const auto whole = [](float value) { return value == std::round(value); };
  const float scale = canvas.image_scale();
  return side > 0 && !rect.IsEmpty() && whole(scale) && whole(rect.x()) &&
         whole(rect.y()) && whole(rect.width()) && whole(rect.height()) &&
         whole(side) && side * scale >= kPieceMinimumPixels &&
         2 * side <= std::min(rect.width(), rect.height());
}

// The squircle's corners `width` by `height` at the origin, rounded `r`, each
// in its radius box, as MakeSquircle draws them: closed along the box's inner
// sides to fill, or, `open`, the curve alone to stroke.
std::array<SkPath, 4> MakeCorners(float width,
                                  float height,
                                  float r,
                                  bool open) {
  const std::array<std::array<gfx::PointF, 4>, 4> corners = {{
      {{{0, r}, {0, 0}, {r, 0}, {r, r}}},
      {{{width - r, 0}, {width, 0}, {width, r}, {width - r, r}}},
      {{{width, height - r},
        {width, height},
        {width - r, height},
        {width - r, height - r}}},
      {{{r, height}, {0, height}, {0, height - r}, {r, height - r}}},
  }};
  std::array<SkPath, 4> paths;
  for (size_t index = 0; index < corners.size(); ++index) {
    const auto& [start, outer, end, center] = corners[index];
    SkPathBuilder path;
    path.moveTo(start.x(), start.y());
    AddCorner(path, start, outer, end, center);
    if (!open) {
      path.lineTo(center.x(), center.y());
      path.close();
    }
    paths[index] = path.detach();
  }
  return paths;
}

// The corners of the squircle `rect` rounded `r` (within half its shorter
// side), cached by size and radius as the whole path is, and placed as it is
// (the same offset of the same points).
std::array<SkPath, 4> Corners(const gfx::RectF& rect, float r, bool open) {
  using Key = std::tuple<float, float, float, bool>;
  static base::NoDestructor<base::LRUCache<Key, std::array<SkPath, 4>>> cache(
      kCachedPaths);
  const Key key(rect.width(), rect.height(), r, open);
  auto found = cache->Get(key);
  if (found == cache->end()) {
    found = cache->Put(key, MakeCorners(rect.width(), rect.height(), r, open));
  }
  std::array<SkPath, 4> corners;
  for (size_t index = 0; index < corners.size(); ++index) {
    corners[index] = found->second[index].makeOffset(rect.x(), rect.y());
  }
  return corners;
}

}  // namespace

SkPath SquirclePath(const gfx::RectF& rect, float radius) {
  const float r = std::min(radius, std::min(rect.width(), rect.height()) / 2);
  if (r <= 0 || rect.IsEmpty()) {
    return SkPath::Rect(
        SkRect::MakeXYWH(rect.x(), rect.y(), rect.width(), rect.height()));
  }
  using Key = std::tuple<float, float, float>;
  static base::NoDestructor<base::LRUCache<Key, SkPath>> cache(kCachedPaths);
  const Key key(rect.width(), rect.height(), r);
  auto found = cache->Get(key);
  if (found == cache->end()) {
    found = cache->Put(key, MakeSquircle(rect.width(), rect.height(), r));
  }
  return found->second.makeOffset(rect.x(), rect.y());
}

SkPath SquircleStrokePath(const gfx::RectF& rect, float radius, float width) {
  gfx::RectF inner = rect;
  inner.Inset(width / 2);
  return SquirclePath(inner, std::max(radius - width / 2, 0.0f));
}

void FillSquircle(gfx::Canvas& canvas,
                  const gfx::RectF& rect,
                  float radius,
                  const cc::PaintFlags& flags) {
  const float r = std::min(radius, std::min(rect.width(), rect.height()) / 2);
  if (!DrawsInPieces(canvas, rect, r)) {
    canvas.DrawPath(SquirclePath(rect, radius), flags);
    return;
  }
  // Between the corners: the band across the middle, and the stretches above
  // and below it.
  for (const gfx::RectF& part :
       {gfx::RectF(rect.x(), rect.y() + r, rect.width(), rect.height() - 2 * r),
        gfx::RectF(rect.x() + r, rect.y(), rect.width() - 2 * r, r),
        gfx::RectF(rect.x() + r, rect.bottom() - r, rect.width() - 2 * r, r)}) {
    if (!part.IsEmpty()) {
      canvas.DrawRect(part, flags);
    }
  }
  for (const SkPath& corner : Corners(rect, r, /*open=*/false)) {
    canvas.DrawPath(corner, flags);
  }
}

void StrokeSquircleBorder(gfx::Canvas& canvas,
                          const gfx::RectF& rect,
                          float radius,
                          float width,
                          const cc::PaintFlags& flags) {
  gfx::RectF line = rect;
  line.Inset(width / 2);
  const float r = std::min(std::max(radius - width / 2, 0.0f),
                           std::min(line.width(), line.height()) / 2);
  // The corners' boxes end where the sides begin: the line's radius and half
  // its width in from the edge.
  const float side = r + width / 2;
  if (r <= 0 || width != std::round(width) ||
      !DrawsInPieces(canvas, rect, side)) {
    canvas.DrawPath(SquircleStrokePath(rect, radius, width), flags);
    return;
  }
  cc::PaintFlags sides = flags;
  sides.setStyle(cc::PaintFlags::kFill_Style);
  for (const gfx::RectF& part :
       {gfx::RectF(rect.x() + side, rect.y(), rect.width() - 2 * side, width),
        gfx::RectF(rect.right() - width, rect.y() + side, width,
                   rect.height() - 2 * side),
        gfx::RectF(rect.x() + side, rect.bottom() - width,
                   rect.width() - 2 * side, width),
        gfx::RectF(rect.x(), rect.y() + side, width,
                   rect.height() - 2 * side)}) {
    if (!part.IsEmpty()) {
      canvas.DrawRect(part, sides);
    }
  }
  cc::PaintFlags corners = flags;
  corners.setStrokeCap(cc::PaintFlags::kButt_Cap);
  for (const SkPath& corner : Corners(line, r, /*open=*/true)) {
    canvas.DrawPath(corner, corners);
  }
}

}  // namespace pane
