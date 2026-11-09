// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_WINDOW_H_
#define PANE_UI_WINDOW_WINDOW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "pane/browser/ui/window_host.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"

namespace content {
class WebContents;
}

namespace ui {
class Compositor;
}

namespace pane {

class RootView;
struct SidebarRow;

// One Pane window in Views: its widget, holding the hidden Browser of each
// profile it shows (WindowHost, ADR 0014), and the root view that draws the
// sidebar, the toolbar, and the selected tab's page. What it shows is the
// app's: the window draws it, takes the keys, and tells the app what its
// Browsers do.
class Window : public WindowHost::Delegate, public ui::AcceleratorTarget {
 public:
  // The app, which knows the profiles and their tabs.
  class Controller {
   public:
    virtual ~Controller() = default;

    // The sidebar's rows: each profile, then its tabs while it is open.
    virtual std::vector<SidebarRow> Rows() = 0;
    virtual void RowPressed(const SidebarRow& row) = 0;
    // Return in the address field, with what it holds.
    virtual void AddressEntered(const std::u16string& text) = 0;
    // ⌘T.
    virtual void NewTab() = 0;
    // ⌘1 to ⌘8: the profile's tab at that place; ⌘9, its last.
    virtual void SelectTabAt(int number) = 0;

    // A page joined one of the window's Browsers, opened by `opener` (or
    // none), to show at once or behind.
    virtual void TabInserted(content::WebContents* contents,
                             content::WebContents* opener,
                             bool foreground) = 0;
    // A page closed, rather than going with its window or the engine.
    virtual void TabRemoved(content::WebContents* contents) = 0;
    // Chrome made a page its Browser's active one.
    virtual void ActiveTabChanged(content::WebContents* contents) = 0;
  };

  // `controller` outlives the window.
  Window(const gfx::Rect& bounds, Controller* controller);
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window() override;

  // Shows the window, in front of the app in use only when `activate`.
  void Show(bool activate);
  WindowHost& host() { return *host_; }
  RootView& root() { return *root_; }
  ui::Compositor* compositor() const;

  // Shows `contents`, a page of one of the window's profiles, and makes its
  // profile the window's. A page chosen takes the keyboard, as a tab chosen
  // in Chrome does; ⌘T's keeps it in the address field.
  void ShowTab(content::WebContents* contents, bool chosen);
  // No page shows: the selected tab's comes after the frame that answers the
  // input, or none is selected. The address field shows `address`.
  void ShowNoPage(const std::u16string& address);
  content::WebContents* selected() const;

  // ⌘L: the address field, its text selected.
  void FocusAddress();
  // The sidebar's rows again, from the app.
  void Refresh();

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // WindowHost::Delegate:
  void TabInserted(content::WebContents* contents,
                   content::WebContents* opener,
                   bool foreground) override;
  void TabRemoved(content::WebContents* contents) override;
  void ActiveTabChanged(Browser* browser,
                        content::WebContents* contents) override;
  void WindowWillClose() override;
  gfx::Rect PageBounds() const override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) override;
  views::NativeWidget* CreateNativeWidget(views::Widget* widget) override;
  int NonClientHitTest(const gfx::Point& point) override;

 private:
  void RowPressed(const SidebarRow& row);
  void AddressEntered(const std::u16string& text);

  views::UnhandledKeyboardEventHandler unhandled_keys_;
  const raw_ptr<Controller> controller_;
  raw_ptr<RootView> root_ = nullptr;
  std::unique_ptr<WindowHost> host_;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_WINDOW_H_
