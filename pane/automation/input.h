// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_INPUT_H_
#define PANE_AUTOMATION_INPUT_H_

#include <string>
#include <vector>

namespace views {
class Widget;
}

namespace pane {

// Real input for a scenario (ADR 0012), posted to `widget`'s window through
// the platform's event path, so what handles the user's runs: the window's
// accelerators, the focused view, and the page. `key` is one character ("t",
// "1", "\r" for Return, "\x1b" for Esc, "\t" for Tab); `modifiers` names
// "command", "shift", "control", "option", and "capsLock". False when the
// key has no code.
bool PostKey(views::Widget* widget,
             const std::string& key,
             const std::vector<std::string>& modifiers);

// A click at `point` of the window (from its top left), down then up,
// `clicks` times, as the trackpad's.
void PostClick(views::Widget* widget, int x, int y, int clicks);

// The pointer moving to `point` of the window (from its top left), as the
// window dispatches a mouse move: what a hover answers. The real pointer stays
// where it is.
void PostMove(views::Widget* widget, int x, int y);

// A trackpad scroll by `dy` points at a point of the window (from its top
// left), dispatched through the window to the view under it: negative moves
// the content up, as a swipe up does.
void PostScroll(views::Widget* widget, int x, int y, double dy);

// The modifiers going down, or up with none, with no key yet.
void PostModifiers(views::Widget* widget,
                   const std::vector<std::string>& modifiers);

}  // namespace pane

#endif  // PANE_AUTOMATION_INPUT_H_
