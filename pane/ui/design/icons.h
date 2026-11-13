// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_ICONS_H_
#define PANE_UI_DESIGN_ICONS_H_

#include <optional>
#include <string_view>
#include <vector>

#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace pane {

// The icons Pane draws: Lucide's (ISC; third_party/lucide, NOTICE), the
// Electron app's set, and Pane's own after Aside's extensions menu, on
// Lucide's 24-unit grid (the Swift app's Lucide and Glyph).
enum class Icon {
  // Lucide.
  kArrowLeft,
  kChevronDown,
  kChevronLeft,
  kChevronRight,
  kChevronUp,
  kChevronsDownUp,
  kChevronsUpDown,
  kCircleDashedX,
  kGlobe,
  kMonitor,
  kMoon,
  kPencil,
  kRotateCw,
  kSettings,
  kSquareArrowOutUpRight,
  kSun,
  kTimerOff,
  kTrash2,
  kTriangleAlert,
  kX,
  // Pane's own: the arrow onto a line, three dots, a bell-shaped pin, the pin
  // struck through, and the puzzle.
  kDownload,
  kEllipsis,
  kPin,
  kPinOff,
  kPuzzle,
};

// An icon's SVG elements, as Lucide writes them and the Swift app drew them.
std::string_view IconElements(Icon icon);

// One of an icon's shapes on its grid: stroked with round caps and joins
// (dashed when it has a dash), or filled.
struct IconShape {
  IconShape();
  IconShape(const IconShape&);
  IconShape& operator=(const IconShape&);
  ~IconShape();

  SkPath path;
  bool filled = false;
  // stroke-dasharray's on and off lengths, on the grid.
  std::optional<std::pair<float, float>> dash;
};

// `elements` as shapes: <path d> through Skia's SVG path parser
// (SkParsePath), <circle> and <rect> (its rx circular, as SVG draws it) as
// Skia's own. nullopt for an element or attribute Pane's icons don't use.
std::optional<std::vector<IconShape>> ParseIconElements(
    std::string_view elements);

// The icon's shapes, parsed once.
const std::vector<IconShape>& IconShapes(Icon icon);

// Draws `icon` into `bounds`, a square, in `color`: its 24-unit grid scaled
// to fit, each line `stroke_width` of the grid wide (Lucide's own is 2; a
// menu's, 1.5).
void PaintIcon(gfx::Canvas& canvas,
               Icon icon,
               const gfx::RectF& bounds,
               float stroke_width,
               SkColor color);

}  // namespace pane

#endif  // PANE_UI_DESIGN_ICONS_H_
