// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_WINDOW_H_
#define PANE_UI_WINDOW_WINDOW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/browser/ui/window_host.h"
#include "pane/model/app_settings.h"
#include "pane/model/pin_entry.h"
#include "pane/ui/app/commands.h"
#include "pane/ui/window/root_view.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class WebContents;
}

namespace ui {
class Compositor;
}

namespace pane {

namespace model {
class ToolbarProgress;
}

class AppIconImages;

// Pane's window in Views: its widget, holding the hidden Browser of each
// profile it shows (WindowHost, ADR 0014), and the root view that draws the
// sidebar, the toolbar, and the selected tab's page. What it shows is the
// app's: the window draws it, takes the keys, and tells the app what its
// Browsers do. Closed (the red button, after every page agreed), its widget
// and Browsers go and the tabs stay the app's (ADR 0016); opened again, it is
// a new widget, shown as the app has it.
class Window : public WindowHost::Delegate,
               public ui::AcceleratorTarget,
               public views::WidgetObserver {
 public:
  // The app, which knows the profiles and their tabs, and runs the command
  // table's commands, the window's keys' as the menu bar's.
  class Controller : public CommandRunner {
   public:
    ~Controller() override = default;

    // What the sidebar shows: each profile, then its tabs while it is open.
    virtual SidebarContent Sidebar() = 0;
    // The rest of what the window shows.
    virtual RootView::Shows Shows() = 0;

    // The sidebar's presses.

    // A profile's header: it opens, or collapsing unloads it (its pages go,
    // its tabs stay).
    virtual void ToggleProfile(const std::string& profile_id) = 0;
    virtual void Select(const std::string& tab_id) = 0;
    // A tab's X, or its menu's Close tab.
    virtual void CloseTab(const std::string& tab_id) = 0;
    // A profile's New tab.
    virtual void NewTabIn(const std::string& profile_id) = 0;
    // A profile's menu: Edit profile, Keep loaded, and Delete profile, once
    // its alert's Delete is chosen.
    virtual void EditProfile(const std::string& profile_id) = 0;
    virtual void SetKeepLoaded(const std::string& profile_id,
                               bool keep_loaded) = 0;
    virtual void DeleteProfile(const std::string& profile_id) = 0;
    // The header's Collapse all: every profile collapses, its pages
    // unloading.
    virtual void CollapseAll() = 0;
    // The footer's New and Settings, and its update pill's click and
    // dismiss.
    virtual void NewProfile() = 0;
    virtual void ToggleSettings() = 0;
    virtual void DownloadUpdate() = 0;
    virtual void DismissUpdate() = 0;
    // A drag's drop in the sidebar: the profile at `from` moved to `to`, or
    // a profile's tab, kept as the model keeps any change.
    virtual void MoveProfile(int from, int to) = 0;
    virtual void MoveTab(const std::string& profile_id, int from, int to) = 0;
    // The sidebar's edge: each step of a drag, its end (the width saved),
    // and a double-click (the standard width).
    virtual void ResizeSidebar(double width) = 0;
    virtual void EndSidebarResize() = 0;
    virtual void ResetSidebarWidth() = 0;

    // Return in the address field, with what it holds.
    virtual void AddressEntered(const std::u16string& text) = 0;
    // Esc in the address field: the page takes the keys back.
    virtual void AddressLeft() = 0;
    // The toolbar's Back, Forward, Reload, and Stop.
    virtual void Back() = 0;
    virtual void Forward() = 0;
    virtual void Reload() = 0;
    virtual void Stop() = 0;
    // The selected tab's load, as the address's bar draws it.
    virtual model::ToolbarProgress& LoadProgress() = 0;
    // The skies of the app's icon, as Settings and its picker show them.
    virtual AppIconImages& AppIcons() = 0;
    // The tab switcher's keys but Ctrl-Tab, a command's (Next Tab): letting
    // go of Ctrl selects the tab it shows selected; Esc closes it.
    virtual void CommitSwitcher() = 0;
    virtual void CancelSwitcher() = 0;
    // Quit Pane?'s Cancel, Esc, or a click outside it; and its Quit or
    // Return.
    virtual void CancelQuit() = 0;
    virtual void QuitNow() = 0;

    // Settings: a choice in its menus, as the settings take it; the App icon
    // row's press, a sky in the picker it opens, and the picker's Done; the
    // PIN's buttons, which open the Lock's PIN screen; and Check for updates
    // (the row's download is the pill's).
    virtual void EditSettings(
        base::RepeatingCallback<void(model::AppSettings&)> edit) = 0;
    virtual void OpenAppIconPicker() = 0;
    virtual void ChooseAppIcon(model::AppIcon icon) = 0;
    virtual void CloseAppIconPicker() = 0;
    virtual void ShowPinScreen(model::PinEntry::Mode mode) = 0;
    virtual void CheckForUpdates() = 0;

    // A page joined one of the window's Browsers, opened by `opener` (or
    // none), to show at once or behind.
    virtual void TabInserted(content::WebContents* contents,
                             content::WebContents* opener,
                             bool foreground) = 0;
    // A page closed, rather than going with its window or the engine.
    virtual void TabRemoved(content::WebContents* contents) = 0;
    // Chrome made a page its Browser's active one.
    virtual void ActiveTabChanged(content::WebContents* contents) = 0;
    // The window closed, its pages with it; their tabs stay.
    virtual void WindowClosed() = 0;
  };

  // A window not yet open; `controller` outlives it. One that `keeps_frame`
  // comes back where the last one was (the Mac's saved frame, "NSWindow
  // Frame PaneMainWindow"); a scenario's copy never reads or writes it, as
  // it shares the user's preferences (B0).
  Window(Controller* controller, bool keeps_frame);
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window() override;

  // Makes the window's widget, unless it has one: 1280 × 800 in the middle
  // of the screen, or where the window last was. Shown by Show.
  void Open();
  // Whether it has a widget, open or closing.
  bool is_open() const { return !!host_; }
  // Shows the window, in front of the app in use only when `activate`.
  void Show(bool activate);
  // The window becomes the key one, the app in front: what a click on it
  // does first.
  void Activate();

  WindowHost* host() { return host_.get(); }
  views::Widget* widget() const;
  // While open.
  RootView* root() { return root_; }
  ui::Compositor* compositor() const;

  // The profile's Browser in this window, made on first use; null while the
  // window is closed or closing.
  Browser* BrowserFor(Profile* profile);

  // Shows `contents`, a page of one of the window's profiles, and makes its
  // profile the window's. A page chosen takes the keyboard, as a tab chosen
  // in Chrome does; ⌘T's keeps it in the address field.
  void ShowTab(content::WebContents* contents, bool chosen);
  // No page shows: the selected tab's comes after the frame that answers the
  // input, or none is selected. The title follows that frame.
  void ShowNoPage();
  // The page leaves the window, as its tab unloads.
  void ClearPage();
  content::WebContents* selected() const;
  // The window's title follows the shown page's, as the Window menu and
  // Mission Control show it: Pane without one, and so behind the Lock.
  void UpdateTitle();
  // The same, once the frame that shows a tab, or none, is out, and after
  // the page made after that frame: AppKit lays the title bar out again at
  // each new title, though Pane's is hidden, which took 1.7 of a tab
  // switch's 2 ms of work ahead of its frame, and 1 of ⌘T's 4.
  void UpdateTitleAfterFrame();
  // The shown page fills the window (ADR 0025).
  bool page_fullscreen() const;
  // The window in full screen, or on its way in; View › Enter or Exit Full
  // Screen toggles it, the page's fullscreen leaving with it (ADR 0025).
  bool IsFullScreen() const;
  void ToggleFullScreen();

  // ⌘L: the address field, its text selected.
  void FocusAddress();
  // The sidebar's rows, and the rest of what shows, again from the app.
  void Refresh();
  // The sidebar at `width`, a step of a drag of its edge, laid out at once.
  void SetSidebarWidth(double width);

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
  void FullscreenChanged(content::WebContents* contents,
                         bool fullscreen) override;
  void WindowWillClose() override;
  gfx::Rect PageBounds() const override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) override;
  views::NativeWidget* CreateNativeWidget(views::Widget* widget) override;
  int NonClientHitTest(const gfx::Point& point) override;

  // views::WidgetObserver:
  void OnWidgetShowStateChanged(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // The switcher's keys while it shows, from Pane's views (the page's come
  // through PreHandleKeyboardEvent): Esc, and Ctrl's release.
  class SwitcherKeys;

  // While the switcher shows, a key from Views or the page: Esc closes it.
  // Whether the key stops here.
  bool SwitcherEscape(bool pressed, int key_code);
  // Ctrl let go while the switcher shows: the tab it shows selected comes
  // forward, as the Swift's key monitor had it. Heard from the frame, in the
  // order the keys came, and from Views: never from a page, whose own key
  // events reach the window late (a key-up from before the switcher showed
  // would arrive after it), and none at all after a key the window took
  // (RenderWidgetHostImpl drops them).
  void ControlReleased();

  // The window's full screen follows the page's: it enters with the page and
  // leaves with it, unless the user had it in full screen already, which
  // stays (ADR 0025). Chromium's fullscreen controller takes a request made
  // while the window is still moving in or out once the move ends.
  void FollowPage();
  // The closed widget's host goes, once its widget has.
  void Closed();

  views::UnhandledKeyboardEventHandler unhandled_keys_;
  std::unique_ptr<SwitcherKeys> switcher_keys_;
  const raw_ptr<Controller> controller_;
  const bool keeps_frame_;
  raw_ptr<RootView> root_ = nullptr;
  std::unique_ptr<WindowHost> host_;
  // The window is in full screen for the page, not the user.
  bool full_screen_for_page_ = false;
  // A title is owed once the frame under way is out.
  bool title_owed_ = false;
  base::WeakPtrFactory<Window> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_WINDOW_H_
