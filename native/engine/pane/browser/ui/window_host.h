// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's WindowHost (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.{h,mm}, Copyright 2026 Mantra
// Patel, Apache License 2.0; native/third_party/ardia): the window as a Chrome
// Views widget with the app's view inside, one hidden Browser per profile, and
// the close sequence. Rewritten for Pane's API, without Ardia's app-specific
// features.

#ifndef PANE_BROWSER_UI_WINDOW_HOST_H_
#define PANE_BROWSER_UI_WINDOW_HOST_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

@class NSView;
@class NSWindow;
@class PaneBrowserWindow;

class Browser;
class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace views {
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

  WindowHost(PaneBrowserWindow* owner, const gfx::Rect& bounds);
  WindowHost(const WindowHost&) = delete;
  WindowHost& operator=(const WindowHost&) = delete;
  ~WindowHost() override;

  static const std::vector<WindowHost*>& All();
  static WindowHost* ForNSWindow(NSWindow* window);
  static WindowHost* ForBrowser(const BrowserWindowInterface* browser);

  NSWindow* ns_window() const;
  NSView* host_view() const { return host_view_; }
  views::Widget* widget() const { return widget_.get(); }
  PaneBrowserWindow* owner() const { return owner_; }

  // Tells the app what to say while a page has fullscreen or a lock (empty:
  // nothing).
  void ShowExclusiveAccessHint(const std::u16string& hint);

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

  // Closing runs every page's beforeunload, then asks about downloads that
  // closing would cancel, then closes every Browser and the widget.
  void Close();
  bool closing() const { return close_state_ == CloseState::kClosing; }
  void ConfirmCloseWithDownloads(int count,
                                 base::OnceCallback<void(bool)> callback);
  bool CanCloseWidget() const { return widget_close_allowed_; }
  void CloseRequestedByUser();

  PendingOpen& pending_open() { return pending_open_; }

  // From HostedBrowserWindow.
  void BrowserWindowDestroyed(HostedBrowserWindow* window);
  void ActiveTabChanged(Browser* browser, content::WebContents* contents);
  void FullscreenChanged(content::WebContents* contents, bool fullscreen);
  bool acted_fullscreen() const { return false; }
  bool internal() const { return false; }

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

  void AddBrowser(Browser* browser);
  void ContinueClose();
  void ContinueCloseAttempt(int attempt);
  void OnBeforeUnloadAnswered(bool proceed);
  void CancelClose();
  void CommitClose();
  void CloseWidget();
  void NotifyWillClose();
  int DownloadsCancelledByClose() const;

  __weak PaneBrowserWindow* owner_;
  // Keeps the app's object alive until the widget closes.
  PaneBrowserWindow* __strong owner_ref_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  NSView* __strong host_view_;
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

// Sets the hooks of pane/browser/ui/hooks.h. Called once, before the first
// Browser exists.
void InstallBrowserHooks();

// Routes Chrome's dialogs and bubbles for Pane's tabs to the app
// (ui_hooks.mm). Runs as the browser starts, before any tab exists.
void InstallUIHooks();

}  // namespace pane

#endif  // PANE_BROWSER_UI_WINDOW_HOST_H_
