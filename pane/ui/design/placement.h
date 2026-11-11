// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_PLACEMENT_H_
#define PANE_UI_DESIGN_PLACEMENT_H_

namespace gfx {
class RectF;
}

namespace views {
class View;
class Widget;
}  // namespace views

namespace pane {

// Puts `view` at `bounds` in its parent, fractions of a point included, as
// SwiftUI places a view on the screen's pixels: half points at 2×, where
// Views lays out on whole points. Its bounds go to the whole points at the
// place's top left, and its layer moves by the rest, rounded to the screen's
// pixels at `scale`, so it draws as crisply as at a whole point and takes
// presses where it shows. The layer, once made, stays.
void PlaceOnPixels(views::View& view, const gfx::RectF& bounds, float scale);

// The screen's pixels per point where `widget` shows, 1 without one.
float ScaleOf(const views::Widget* widget);

}  // namespace pane

#endif  // PANE_UI_DESIGN_PLACEMENT_H_
