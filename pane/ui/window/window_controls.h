// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_WINDOW_CONTROLS_H_
#define PANE_UI_WINDOW_WINDOW_CONTROLS_H_

#include <memory>

#include "ui/views/view.h"

namespace gfx {
class Point;
class Rect;
}  // namespace gfx

namespace pane {

// The window's own controls where Pane draws them: Windows' caption buttons,
// Minimize, Maximize or Restore, and Close, at the toolbar's end in the
// content panel's top-right corner, Chrome's place with no tab strip across
// the top (Paper 11.1's B), and in that corner under the lock and in
// Settings, where no toolbar shows (07.x, 06.4.1). They look and move as
// Chrome's WindowsCaptionButton (11.2), and answer Windows' hit test as its
// own buttons, so Maximize opens Snap Layouts (11.3). The Mac has none: its
// traffic lights are the frame's (frame_mac.mm).
class WindowControls : public views::View {
  METADATA_HEADER(WindowControls, views::View)

 public:
  // Windows' (window_controls_win.cc); none on the Mac.
  static std::unique_ptr<WindowControls> Make();

  WindowControls(const WindowControls&) = delete;
  WindowControls& operator=(const WindowControls&) = delete;
  ~WindowControls() override;

  // The buttons' row, 45 by 42 each and 1 apart, 137 wide, which the
  // toolbar's end keeps for them.
  virtual int RowWidth() const = 0;
  virtual int RowHeight() const = 0;

  // Lays the row out with its top right at `panel`'s, the content panel in
  // this view's coordinates, whose corner rounds Close's fill. This view
  // reaches past it into the frame where the frame's gutter answers as Close,
  // above it and beside it, so the screen's corner pixel presses Close in a
  // maximized window (11.2); else it holds the row alone.
  virtual void SetPanel(const gfx::Rect& panel) = 0;

  // The system's code for a press at `point`, in this view: HTMINBUTTON,
  // HTMAXBUTTON, or HTCLOSE, else HTNOWHERE.
  virtual int ComponentAt(const gfx::Point& point) = 0;

 protected:
  WindowControls();
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_WINDOW_CONTROLS_H_
