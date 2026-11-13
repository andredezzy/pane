// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

#include <windows.h>

#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_tree_host.h"
#include "ui/events/event.h"
#include "ui/events/event_target.h"
#include "ui/events/event_targeter.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The aura window a press at `point` (in the window, from its top left)
// reaches, as the window's dispatcher finds it: the root window's own
// targeter, else the dispatcher's (ui::EventProcessor's order), which a
// page's window and Views' content window answer.
aura::Window* PressedWindow(views::Widget* widget, const gfx::Point& point) {
  aura::Window* window = widget->GetNativeWindow();
  aura::Window* root = window ? window->GetRootWindow() : nullptr;
  aura::WindowTreeHost* host = root ? root->GetHost() : nullptr;
  if (!host) {
    return nullptr;
  }
  ui::EventTargeter* targeter =
      static_cast<ui::EventTarget*>(root)->GetEventTargeter();
  if (!targeter) {
    targeter = host->dispatcher()->GetDefaultEventTargeter();
  }
  ui::MouseEvent press(ui::EventType::kMousePressed, gfx::PointF(point),
                       gfx::PointF(point), ui::EventTimeForNow(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  return static_cast<aura::Window*>(targeter->FindTargetForEvent(root, &press));
}

}  // namespace

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

// A page's own window (RenderWidgetHostViewAura) takes the press, or Views'
// content window, where Views answers HTCAPTION for a part that moves the
// window, as Windows asks it (WM_NCHITTEST); the system's frame, around the
// client area these points cover, is the frame's own.
PressTarget PressTargetAt(views::Widget* widget, const gfx::Point& point) {
  aura::Window* pressed = PressedWindow(widget, point);
  if (!pressed) {
    return PressTarget::kOther;
  }
  for (aura::Window* window = pressed; window; window = window->parent()) {
    if (window->GetName() == "RenderWidgetHostViewAura") {
      return PressTarget::kPage;
    }
  }
  switch (widget->GetNonClientComponent(point)) {
    case HTCAPTION:
      return PressTarget::kDrag;
    case HTCLIENT:
    case HTNOWHERE:
      return PressTarget::kViews;
    default:
      return PressTarget::kOther;
  }
}

std::string PlatformViewAt(views::Widget* widget, const gfx::Point& point) {
  aura::Window* pressed = PressedWindow(widget, point);
  return pressed ? std::string(pressed->GetName()) : std::string();
}

void KeepFrame(views::Widget* widget) {}

std::vector<gfx::Rect> WindowControlFrames(views::Widget* widget) {
  return {};
}

}  // namespace pane
