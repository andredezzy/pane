// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_FRAME_H_
#define PANE_UI_WINDOW_FRAME_H_

#include <string>
#include <vector>

#include "ui/gfx/geometry/rect.h"

namespace gfx {
class Point;
}

namespace views {
class NativeWidget;
class Widget;
}  // namespace views

namespace pane {

// A Pane window's native widget, framed as the platform frames Pane's
// windows: on the Mac, a full-size content view under a transparent title
// bar, the traffic lights at (12, 12) (ADR 0036); on Windows, the caption
// buttons. Owned by `widget`.
views::NativeWidget* CreateFrame(views::Widget* widget);

// Who takes a press at `point` (in the window, from its top left): a page,
// or Pane's views, Views having first say over what a view of its covers,
// as a sheet or its scrim over a page (bridged_content_view.mm, -hitTest:);
// or the frame, which moves the window, where Views answers HTCAPTION; or
// the frame's own controls (the traffic lights).
enum class PressTarget { kPage, kViews, kDrag, kOther };
PressTarget PressTargetAt(views::Widget* widget, const gfx::Point& point);
// The platform's own view a press there lands on, by its class: what the
// channel reports beside the target.
std::string PlatformViewAt(views::Widget* widget, const gfx::Point& point);

// Where the window last was, and where it goes from now on: the Mac's frame
// saved under "NSWindow Frame PaneMainWindow" in Pane's preferences, read as
// the window opens and written as it moves, as the Swift app's was. A window
// with none keeps the bounds it has.
void KeepFrame(views::Widget* widget);

// The window's own controls (the Mac's traffic lights, Windows' caption
// buttons), each from the window's top left, in their order: what a
// scenario reads to check where the frame put them.
std::vector<gfx::Rect> WindowControlFrames(views::Widget* widget);

}  // namespace pane

#endif  // PANE_UI_WINDOW_FRAME_H_
