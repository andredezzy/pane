// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's WindowHost (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.{h,mm}, Copyright 2026 Mantra
// Patel, Apache License 2.0; third_party/ardia): the window as a Chrome
// Views widget with the app's view inside, one hidden Browser per profile, and
// the close sequence. Rewritten for Pane's API, without Ardia's app-specific
// features.

#ifndef PANE_BROWSER_UI_WINDOW_HOST_H_
#define PANE_BROWSER_UI_WINDOW_HOST_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "pane/browser/ui/install_hooks.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

#if defined(__OBJC__)
@class NSView;
@class NSWindow;
@class PaneBrowserWindow;
#endif

class Browser;
class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace input {
struct NativeWebKeyboardEvent;
}

namespace views {
class NativeWidget;
class View;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace pane {

class HostedBrowserWindow;

// One Pane window. Chrome sees a Views widget (so its own dialogs and bubbles
// have a window to attach to) holding the app's view, and one Browser per
// profile shown in the window, each with a HostedBrowserWindow: no
// BrowserView, no tab strip or toolbar of Chrome's. A tab's view goes straight
// into the app's views; switching profiles is a view swap in the app plus
// SetActiveProfile here.
class WindowHost : public TabStripModelObserver,
                   public views::WidgetObserver,
                   public web_modal::WebContentsModalDialogHost {
 public:
  // How the tab being inserted was opened, for the insert notification.
  struct PendingOpen {
    std::optional<WindowOpenDisposition> disposition;
    base::WeakPtr<content::WebContents> source;
  };

  // Pane's Views UI, told what the window's Browsers do, as the Swift app is
  // told through PaneBrowserWindow's delegate until it goes.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // A tab joined one of the window's Browsers, opened by `opener` (or
    // none), to show at once or behind.
    virtual void TabInserted(content::WebContents* contents,
                             content::WebContents* opener,
                             bool foreground) {}
    // A tab left, closed rather than carried off by a closing window.
    virtual void TabRemoved(content::WebContents* contents) {}
    virtual void ActiveTabChanged(Browser* browser,
                                  content::WebContents* contents) {}
    virtual void FullscreenChanged(content::WebContents* contents,
                                   bool fullscreen) {}
    // What to say while a page has fullscreen or a lock (empty: nothing).
    virtual void ShowExclusiveAccessHint(const std::u16string& hint) {}
    virtual void WindowWillClose() {}
    // Where the page is, in the widget's root view: Chrome's tab-modal
    // dialogs show over it. It moved: NotifyPositionRequiresUpdate.
    virtual gfx::Rect PageBounds() const = 0;
    // A key on its way to the page, which the app may take first (⌘T, ⌘L).
    virtual content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
        const input::NativeWebKeyboardEvent& event);
    // A key the page left unhandled.
    virtual bool HandleKeyboardEvent(
        const input::NativeWebKeyboardEvent& event);
    // The widget's native widget, framed as the platform frames Pane's
    // windows; null for the default.
    virtual views::NativeWidget* CreateNativeWidget(views::Widget* widget);
    // What a point of the window is, as Windows' WM_NCHITTEST names it:
    // HTCAPTION drags the window, on the Mac too; HTNOWHERE defers to Views.
    virtual int NonClientHitTest(const gfx::Point& point);
  };

#if defined(__OBJC__)
  // A window of the Swift app's, which holds its view (PaneBrowserWindow).
  WindowHost(PaneBrowserWindow* owner, const gfx::Rect& bounds);
#endif
  // A window of Pane's Views UI: `contents` fills the widget, and `delegate`
  // outlives the WindowHost.
  WindowHost(Delegate* delegate,
             const gfx::Rect& bounds,
             std::unique_ptr<views::View> contents);
  WindowHost(const WindowHost&) = delete;
  WindowHost& operator=(const WindowHost&) = delete;
  ~WindowHost() override;

  static const std::vector<WindowHost*>& All();
  static WindowHost* ForBrowser(const BrowserWindowInterface* browser);

#if defined(__OBJC__)
  static WindowHost* ForNSWindow(NSWindow* window);
  NSWindow* ns_window() const;
  NSView* host_view() const;
  PaneBrowserWindow* owner() const;
#endif
  views::Widget* widget() const { return widget_.get(); }
  Delegate* delegate() const { return delegate_; }

  // Tells the app what to say while a page has fullscreen or a lock (empty:
  // nothing).
  void ShowExclusiveAccessHint(const std::u16string& hint);

  // The window's title, as the Window menu and Mission Control show it.
  void SetTitle(const std::u16string& title);

  // The profile's Browser in this window, made on first use; nullptr while the
  // window or the profile is going away.
  Browser* BrowserFor(Profile* profile);
  Browser* ExistingBrowserFor(Profile* profile) const;
  std::vector<Browser*> browsers() const;

  // Hosts a Browser Chrome made itself (undocked DevTools): as BrowserFor's,
  // once it is fully built. With `close_when_empty`, the window goes when its
  // last Browser does.
  void AdoptBrowser(Browser* browser, bool close_when_empty);

  // The profile the app shows: its Browser is the window's active one.
  void SetActiveProfile(Profile* profile);
  Profile* active_profile() const { return active_profile_; }
  bool IsActiveBrowser(const Browser* browser) const;
  void RequestActivation(Browser* browser);

  // Closing runs every page's beforeunload, then closes every Browser and
  // the widget. Downloads are the profile's, not the window's: they go on.
  void Close();
  bool closing() const { return close_state_ == CloseState::kClosing; }
  bool CanCloseWidget() const { return widget_close_allowed_; }
  void CloseRequestedByUser();

  PendingOpen& pending_open() { return pending_open_; }

  // From HostedBrowserWindow.
  void BrowserWindowDestroyed(HostedBrowserWindow* window);
  void ActiveTabChanged(Browser* browser, content::WebContents* contents);
  void FullscreenChanged(content::WebContents* contents, bool fullscreen);

  // The content area moved or changed size: Chrome's tab-modal dialogs over
  // it follow, on the next turn.
  void ScheduleDialogPositionUpdate();

  base::WeakPtr<WindowHost> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // web_modal::WebContentsModalDialogHost:
  gfx::NativeView GetHostView() const override;
  gfx::Point GetDialogPosition(const gfx::Size& size) override;
  bool ShouldActivateDialog() const override;
  gfx::Size GetMaximumDialogSize() override;
  void AddObserver(web_modal::ModalDialogHostObserver* observer) override;
  void RemoveObserver(web_modal::ModalDialogHostObserver* observer) override;
  void NotifyPositionRequiresUpdate() override;

 private:
  enum class CloseState { kOpen, kAsking, kClosing };

  void InitWidget(const gfx::Rect& bounds,
                  std::unique_ptr<views::View> contents);
  // Where tab-modal dialogs show: the page's place, or the whole window
  // (the Swift app's).
  gfx::Rect ContentArea() const;
  void AddBrowser(Browser* browser);
  void ContinueClose();
  void ContinueCloseAttempt(int attempt);
  void OnBeforeUnloadAnswered(bool proceed);
  void CancelClose();
  void CommitClose();
  void CloseWidget();
  void NotifyWillClose();

  // The Swift app's objects, Objective-C, until it goes: its window object,
  // kept alive until the widget closes, and the view it draws in.
  struct Cocoa;

  raw_ptr<Delegate> delegate_ = nullptr;
  std::unique_ptr<Cocoa> cocoa_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::map<Profile*, raw_ptr<Browser>> browsers_;
  raw_ptr<Profile> active_profile_ = nullptr;
  CloseState close_state_ = CloseState::kOpen;
  raw_ptr<Browser> asking_ = nullptr;
  int close_attempt_ = 0;
  bool widget_close_allowed_ = false;
  bool close_when_empty_ = false;
  bool notified_will_close_ = false;
  PendingOpen pending_open_;
  base::ObserverList<web_modal::ModalDialogHostObserver> modal_observers_;
  bool dialog_position_update_scheduled_ = false;
  base::WeakPtrFactory<WindowHost> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_WINDOW_HOST_H_
