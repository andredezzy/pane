// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_FRAME_H_
#define PANE_UI_WINDOW_FRAME_H_

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
// as a sheet or its scrim over a page (bridged_content_view.mm, -hitTest:).
enum class PressTarget { kPage, kViews, kOther };
PressTarget PressTargetAt(views::Widget* widget, const gfx::Point& point);

}  // namespace pane

#endif  // PANE_UI_WINDOW_FRAME_H_
