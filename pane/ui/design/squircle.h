// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_SQUIRCLE_H_
#define PANE_UI_DESIGN_SQUIRCLE_H_

#include "third_party/skia/include/core/SkPath.h"

namespace cc {
class PaintFlags;
}

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace pane {

// The design's radii (DESIGN.md › Shape), each 1.84× the circular one it
// replaced (ADR 0057), and their uses there.
namespace radius {
// Favicons, the active profile's square.
inline constexpr float kXs = 4;
// Icon buttons, a dialog's fields.
inline constexpr float kSm = 7;
// The address bar, chips.
inline constexpr float kTab = 9;
// Buttons, fields, swatches.
inline constexpr float kMd = 11;
// Sidebar rows, menu rows.
inline constexpr float kLg = 15;
// The content panel, the profile sheet.
inline constexpr float kPanel = 18;
// Menus, the tab switcher.
inline constexpr float kXl = 22;
// Dialogs.
inline constexpr float k2xl = 29;
}  // namespace radius

// What a layer, whose corners are only circular, takes for a squircle of a
// radius: a circle of 0.57 of it follows the superellipse within 0.012 of the
// radius, a fifth of a point at the panel's 18 (ADR 0057), with no mask to
// composite.
inline constexpr float kCircularEquivalent = 0.57f;

// The content panel's corners as a layer draws them: they clip the page, and
// Settings, which scrolls in the page's place.
inline constexpr float kPanelLayerRadius = radius::kPanel * kCircularEquivalent;

// Every rounded corner in Pane: in each corner's radius box, the quarter of
// |x|⁴ + |y|⁴ = 1 (Paper's `corner-shape: superellipse(2)`), each corner two
// cubics as Blink draws one (path_builder.cc), clockwise from the top left
// corner's start. The radius shrinks to half the shorter side, as a rounded
// rectangle's does; none, or an empty rectangle, is the rectangle. Cached by
// size and radius, so a view painting each frame of a hover builds none.
SkPath SquirclePath(const gfx::RectF& rect, float radius);

// The line `width` wide drawn just inside the squircle's edge, as SwiftUI's
// strokeBorder draws one: the squircle inset by half the width, its radius
// shrinking with it, to stroke at `width`. A border's, and a ring's outside
// a shape when given the ring's own bounds and radius.
SkPath SquircleStrokePath(const gfx::RectF& rect, float radius, float width);

// Fills SquirclePath(rect, radius) with `flags` in pieces a tile's raster
// takes cheaply: rectangles between the corners, and each corner a path of
// its own in its radius box. Skia works a path's edges out for every tile it
// rasters, so a card painted as one path cost each tile under it the whole
// path (Settings' first frame rastered in 5.0 ms, and in pieces 4.0: ADR
// 0123); a tile away from the corners now fills rectangles. The pieces meet
// on whole pixels and cover each one as the whole path does
// (squircle_unittest.cc); where they wouldn't, a corner under 33 pixels
// (which Skia antialiases another way) or off whole pixels, it fills the one
// path.
void FillSquircle(gfx::Canvas& canvas,
                  const gfx::RectF& rect,
                  float radius,
                  const cc::PaintFlags& flags);

// Strokes SquircleStrokePath(rect, radius, width) at `width` with `flags`, a
// border just inside the squircle's edge, in the same pieces: its sides as
// rectangles, and each corner's curve stroked alone, its ends cut square
// where the sides begin.
void StrokeSquircleBorder(gfx::Canvas& canvas,
                          const gfx::RectF& rect,
                          float radius,
                          float width,
                          const cc::PaintFlags& flags);

}  // namespace pane

#endif  // PANE_UI_DESIGN_SQUIRCLE_H_
