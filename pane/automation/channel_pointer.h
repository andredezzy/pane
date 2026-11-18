// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_CHANNEL_POINTER_H_
#define PANE_AUTOMATION_CHANNEL_POINTER_H_

#include <memory>

#include "base/functional/callback_helpers.h"

namespace display {
class Screen;
}

namespace views {
class Widget;
}

namespace pane {

// Where a scenario's copy finds the pointer when it asks the screen for it,
// as Views does when a button shows (View::IsMouseHovered), a scroll moves
// what is under it (Widget::SynthesizeMouseMoveEvent), or a menu opens: at
// the channel's last click, move, scroll, or drag (input.h), and before the
// first nowhere on any screen. Never the system's pointer, which is the
// user's own while a scenario runs behind their windows: under it a key of
// the PIN pad showed hovered, in one run and not the next (ADR 0128). The
// channel holds one while it runs, standing in for the system's
// display::Screen, which it asks for everything else. And while the copy may
// not take the front, the system's own pointer events, which reach a window
// behind where it shows and while a launch holds the front for a moment, are
// kept from it: the channel's go to the window itself.
class ChannelPointer {
 public:
  ChannelPointer();
  ChannelPointer(const ChannelPointer&) = delete;
  ChannelPointer& operator=(const ChannelPointer&) = delete;
  ~ChannelPointer();

  // The channel's pointer gone to (x, y) of `widget`'s window, from its top
  // left, as input.h's points are: it stays at that point of the screen.
  static void MovedTo(views::Widget* widget, int x, int y);

 private:
  // The system's own moves, enters, exits, drags, and scrolls kept from the
  // app while it may not take the front (each system's file).
  static base::ScopedClosureRunner KeepSystemPointerOut();

  std::unique_ptr<display::Screen> screen_;
  base::ScopedClosureRunner system_pointer_out_;
};

}  // namespace pane

#endif  // PANE_AUTOMATION_CHANNEL_POINTER_H_
