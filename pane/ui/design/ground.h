// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_GROUND_H_
#define PANE_UI_DESIGN_GROUND_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_variant.h"

namespace views {
class View;
}  // namespace views

namespace pane {

// The opaque color a surface paints across its bounds, under whatever text
// it holds: the window's background under the sidebar, the panel's card
// under the toolbar and Settings, a dialog's card under its lines. A control
// whose fill moves over it (a row under the pointer, a pressed button) names
// the color the two make, as it moves. A label takes the nearest one up its
// parents as the ground of its text (SetLabelGround), which on Windows draws
// ClearType there (Paper 11.8); on the Mac nothing changes.
void SetGround(views::View& surface, ui::ColorVariant ground);

// The ground under `view`: the nearest named up from it, `view` included,
// resolved for its theme; transparent where there is none, or none resolves
// yet.
SkColor GroundOf(const views::View& view);

// Gives each of Pane's labels under `view` its ground again, from the
// nearest one up from it: for a control whose fill moved, or a surface whose
// ground changed.
void RefreshLabelGrounds(views::View& view);

// `fill` laid over `ground` at `opacity` of its own alpha, as a view paints
// one over the other: what a control's moving fill and the ground under it
// make together.
SkColor OverGround(SkColor fill, double opacity, SkColor ground);

}  // namespace pane

#endif  // PANE_UI_DESIGN_GROUND_H_
