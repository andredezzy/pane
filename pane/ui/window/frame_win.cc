// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

namespace pane {

// Pane's own frame on Windows, its caption buttons, Snap Layouts, the place it
// keeps between launches, and Ctrl's release heard ahead of the page, is W4's
// (docs/plans/views-migration.md): until then the window has the system's own
// frame, Views' default native widget's.
views::NativeWidget* CreateFrame(
    views::Widget* widget,
    base::RepeatingCallback<bool()> takes_every_key,
    base::RepeatingCallback<void(bool control_down)> control_changed) {
  return nullptr;
}

// What a scenario's press lands on, as the channel reads it, is W6's.
PressTarget PressTargetAt(views::Widget* widget, const gfx::Point& point) {
  return PressTarget::kOther;
}

std::string PlatformViewAt(views::Widget* widget, const gfx::Point& point) {
  return std::string();
}

void KeepFrame(views::Widget* widget) {}

std::vector<gfx::Rect> WindowControlFrames(views::Widget* widget) {
  return {};
}

}  // namespace pane
