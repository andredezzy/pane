// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_PLACEMENT_H_
#define PANE_UI_DESIGN_PLACEMENT_H_

#include "ui/gfx/geometry/point_f.h"

namespace gfx {
class RectF;
}

namespace ui {
class LocatedEvent;
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

// Where `event` is in `view`'s coordinates, to the fraction of a point. Views
// carries an event into the view it goes to on whole points
// (ui::LocatedEvent::ConvertLocationToTarget floors it), so in a view its
// layer moves a fraction of a point, a row sliding, the event's place comes
// up to a point short, by where the layer is at that moment. This reads it
// from the event's place in the window instead, its root location, in the
// coordinates of `view`'s topmost ancestor, the root view.
gfx::PointF PointIn(const views::View& view, const ui::LocatedEvent& event);

// The screen's pixels per point where `widget` shows, 1 without one.
float ScaleOf(const views::Widget* widget);

}  // namespace pane

#endif  // PANE_UI_DESIGN_PLACEMENT_H_
