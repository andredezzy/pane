// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

#include <windows.h>

#include <memory>
#include <utility>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/shell_integration_win.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_targeter.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/win/shell.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/event_target.h"
#include "ui/events/event_targeter.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/view.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
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

// Ctrl going down or up, heard on the window's root ahead of every target,
// a page's own window (RenderWidgetHostViewAura) among them, where the
// switcher's views never hear it: the page takes a key from aura's
// dispatcher, past Views.
class ControlKeys : public ui::EventHandler {
 public:
  explicit ControlKeys(base::RepeatingCallback<void(bool)> control_changed)
      : control_changed_(std::move(control_changed)) {}

  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override {
    const ui::KeyboardCode code = event->key_code();
    if (code != ui::VKEY_CONTROL && code != ui::VKEY_LCONTROL &&
        code != ui::VKEY_RCONTROL) {
      return;
    }
    if (event->type() == ui::EventType::kKeyPressed) {
      control_changed_.Run(true);
    } else if (event->type() == ui::EventType::kKeyReleased &&
               !event->IsControlDown()) {
      control_changed_.Run(false);
    }
  }

 private:
  const base::RepeatingCallback<void(bool)> control_changed_;
};

// Views first over what a view of its covers, as on the Mac (Views' own
// hit test there, NativeWidgetMacNSWindowHost::GetHitTestResult): a page's
// window, a child of the widget's, takes a press, a move, or a wheel only
// where Views' own target there is the page's host (a NativeViewHost); where
// a view of Pane's lies over it (a sheet, its scrim, the extensions menu's
// shield), Views' content window takes it. aura, by itself, gives every
// event over a page's window to the page.
class ViewsFirst : public aura::WindowTargeter {
 public:
  ViewsFirst(views::Widget* widget, aura::Window* content)
      : widget_(widget->GetWeakPtr()), content_(content) {}

  // aura::WindowTargeter:
  bool SubtreeShouldBeExploredForEvent(aura::Window* window,
                                       const ui::LocatedEvent& event) override {
    if (!aura::WindowTargeter::SubtreeShouldBeExploredForEvent(window, event)) {
      return false;
    }
    views::View* root = widget_ ? widget_->GetRootView() : nullptr;
    if (window->parent() != content_ || !root) {
      return true;
    }
    // The event's place in the content window's coordinates, which are the
    // root view's, whichever window the targeter has got to.
    gfx::Point point = event.root_location();
    aura::Window::ConvertPointToTarget(content_->GetRootWindow(), content_,
                                       &point);
    views::View* target = root->GetEventHandlerForPoint(point);
    return !target || views::IsViewClass<views::NativeViewHost>(target);
  }

 private:
  const base::WeakPtr<views::Widget> widget_;
  const raw_ptr<aura::Window> content_;
};

// A key goes to the window that holds the focus, and aura drops it, before any
// handler hears it, when that window can't take it (Window::CanAcceptEvent):
// a page's view focused while it is still hidden, as a tab just reopened or
// woken is from its renderer's frame until it shows (WebView focuses it then,
// WebView::SetUpNewMainFrame). Ctrl+Shift+T then Ctrl+1 lost the Ctrl+1 so. A
// key the focused window can't take goes to Views' content window instead,
// where the window's shortcuts hear it (PageWebView leaves them the keys while
// the page doesn't show), and any other key goes nowhere, as it did.
class KeysToTheWindow : public aura::WindowTargeter {
 public:
  explicit KeysToTheWindow(aura::Window* content) : content_(content) {}

  // aura::WindowTargeter:
  ui::EventTarget* FindTargetForEvent(ui::EventTarget* root,
                                      ui::Event* event) override {
    ui::EventTarget* target =
        aura::WindowTargeter::FindTargetForEvent(root, event);
    ui::EventTarget* content = content_.get();
    if (event->IsKeyEvent() && target && !target->CanAcceptEvent(*event) &&
        content->CanAcceptEvent(*event)) {
      return content;
    }
    return target;
  }

 private:
  const raw_ptr<aura::Window> content_;
};

// Pane's window on Windows: Views' own native widget, with the system's frame
// (Pane's caption buttons and Snap Layouts are Paper's first, W5), Ctrl heard
// ahead of the page, a key the focused window can't take heard by the
// window's shortcuts, Views first over the page, and Pane's identity on the
// taskbar.
class Frame : public views::DesktopNativeWidgetAura {
 public:
  Frame(views::Widget* widget,
        base::RepeatingCallback<void(bool)> control_changed)
      : views::DesktopNativeWidgetAura(widget),
        control_keys_(std::move(control_changed)) {}

  ~Frame() override { StopHearingKeys(); }

  // views::DesktopNativeWidgetAura:
  void InitNativeWidget(views::Widget::InitParams params) override {
    views::DesktopNativeWidgetAura::InitNativeWidget(std::move(params));
    // Ahead of Views' own handlers on the root, the FocusManager's among
    // them.
    host()->window()->AddPreTargetHandler(&control_keys_,
                                          ui::EventTarget::Priority::kSystem);
    host()->window()->SetEventTargeter(
        std::make_unique<KeysToTheWindow>(content_window()));
    hears_keys_ = true;
    content_window()->SetEventTargeter(
        std::make_unique<ViewsFirst>(GetWidget(), content_window()));
    // Pane's own on the taskbar, its windows grouped under it, never an
    // installed Chromium's: the browser's ID as Chrome gives it its default
    // profile's window, from the install modes Pane's identity names
    // (chrome/install_static, patch 0001). Set before the window first
    // shows, which is when the taskbar reads it.
    ui::win::SetAppIdForWindow(
        shell_integration::win::GetAppUserModelIdForBrowser(base::FilePath()),
        host()->GetAcceleratedWidget());
  }

  void OnHostClosed() override {
    StopHearingKeys();
    views::DesktopNativeWidgetAura::OnHostClosed();
  }

  // Windows tells the widget's observers of a full screen entered or left
  // only through a change of size read against the show state the widget
  // saved, which a Show of a window already in full screen resets
  // (Widget::Show): a reopen, the channel's activate. Then leaving full screen
  // went unheard, and the page stayed in its fullscreen in a window out of it
  // (Window::OnWidgetShowStateChanged). Each change is told here, as Views'
  // own Linux host tells its window's.
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override {
    const bool was = IsFullscreen();
    base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
    views::DesktopNativeWidgetAura::SetFullscreen(fullscreen,
                                                  target_display_id);
    if (widget && was != IsFullscreen()) {
      widget->OnNativeWidgetWindowShowStateChanged();
    }
  }

 private:
  // Before the content window goes, which the root's targeter names.
  void StopHearingKeys() {
    if (hears_keys_ && host()) {
      host()->window()->RemovePreTargetHandler(&control_keys_);
      host()->window()->SetEventTargeter(nullptr);
    }
    hears_keys_ = false;
  }

  ControlKeys control_keys_;
  bool hears_keys_ = false;
};

}  // namespace

// The system's frame, until Pane's caption buttons are drawn (W5): what the
// Mac's frame does besides drawing is done here. While an overlay takes every
// key, Quit's keys still reach the window's Quit (Window::AcceleratorPressed),
// and the window's other commands don't run, so `takes_every_key` asks
// nothing more here.
views::NativeWidget* CreateFrame(
    views::Widget* widget,
    base::RepeatingCallback<bool()> takes_every_key,
    base::RepeatingCallback<void(bool control_down)> control_changed) {
  return new Frame(widget, std::move(control_changed));
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

// Where the window was, kept between launches as the Mac keeps it in its
// preferences, is still to come on Windows (left by W4: where Windows keeps
// it, the registry or the data root, so a test copy never moves the user's,
// is to decide): the window opens in the middle of the screen each time.
void KeepFrame(views::Widget* widget) {}

std::vector<gfx::Rect> WindowControlFrames(views::Widget* widget) {
  return {};
}

}  // namespace pane
