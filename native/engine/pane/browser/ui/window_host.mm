// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's WindowHost (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; native/third_party/ardia). The close sequence follows
// Ardia's closely; the rest is rewritten for Pane's API.

#include "pane/browser/ui/window_host.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_active_state_manager/browser_active_state_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/desktop_browser_window_capabilities.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/browser/ui/views/interaction/browser_elements_views_impl.h"
#import "components/remote_cocoa/app_shim/bridged_content_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/hooks.h"
#include "pane/browser/ui/modal_dialog_host.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/web_contents_delegate.h"
#import "pane/public/PaneBrowserWindow.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/client_view.h"

namespace pane {

namespace {

std::vector<WindowHost*>& Hosts() {
  static base::NoDestructor<std::vector<WindowHost*>> hosts;
  return *hosts;
}

int g_tab_strip_notifying = 0;

// The widget's close button asks the app (and the close sequence) first.
class HostClientView : public views::ClientView {
 public:
  HostClientView(views::Widget* widget,
                 views::View* contents,
                 base::WeakPtr<WindowHost> host)
      : views::ClientView(widget, contents), host_(std::move(host)) {}

  views::CloseRequestResult OnWindowCloseRequested() override {
    if (!host_ || host_->CanCloseWidget()) {
      return views::CloseRequestResult::kCanClose;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&WindowHost::CloseRequestedByUser, host_));
    return views::CloseRequestResult::kCannotClose;
  }

 private:
  base::WeakPtr<WindowHost> host_;
};

// Only one window's Browser is Chrome's active one: extensions' "current
// window", keyboard shortcuts and the like follow it.
void ActivateExclusively(Browser* browser) {
  if (browser->IsDeleteScheduled()) {
    return;
  }
  for (WindowHost* host : WindowHost::All()) {
    for (Browser* other : host->browsers()) {
      if (other != browser) {
        BrowserActiveStateManager::From(other)->DidBecomeInactive();
      }
    }
  }
  BrowserActiveStateManager::From(browser)->DidBecomeActive();
}

}  // namespace

bool IsNotifyingTabStrip() {
  return g_tab_strip_notifying > 0;
}

const std::vector<WindowHost*>& WindowHost::All() {
  return Hosts();
}

WindowHost* WindowHost::ForNSWindow(NSWindow* window) {
  for (WindowHost* host : Hosts()) {
    if (window && host->ns_window() == window) {
      return host;
    }
  }
  return nullptr;
}

WindowHost* WindowHost::ForBrowser(const BrowserWindowInterface* browser) {
  return HostedBrowserWindow::HostOf(browser);
}

WindowHost::WindowHost(PaneBrowserWindow* owner, const gfx::Rect& bounds)
    : owner_(owner), owner_ref_(owner) {
  Hosts().push_back(this);

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanResize(true);
  widget_delegate_->SetCanMaximize(true);
  widget_delegate_->SetCanMinimize(true);
  widget_delegate_->SetCanFullscreen(true);
  widget_delegate_->SetTitle(u"Pane");
  widget_delegate_->SetClientViewFactory(base::BindOnce(
      [](base::WeakPtr<WindowHost> host, views::Widget* widget,
         views::View* contents) -> std::unique_ptr<views::ClientView> {
        return std::make_unique<HostClientView>(widget, contents, host);
      },
      GetWeakPtr()));

  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = widget_delegate_.get();
  params.bounds = bounds;
  params.name = "PaneWindow";
  widget_->Init(std::move(params));
  widget_->AddObserver(this);

  // The app's view covers the widget's: it is hit-tested and read by
  // assistive technologies first (patch 0003, BridgedContentView).
  NSView* content = ns_window().contentView;
  host_view_ = [[NSView alloc] initWithFrame:content.bounds];
  host_view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  host_view_.wantsLayer = YES;
  [content addSubview:host_view_];
  if ([content isKindOfClass:[BridgedContentView class]]) {
    static_cast<BridgedContentView*>(content).paneEmbeddedView = host_view_;
  }
}

WindowHost::~WindowHost() {
  std::erase(Hosts(), this);
  for (auto& observer : modal_observers_) {
    observer.OnHostDestroying();
  }
  for ([[maybe_unused]] auto& observer : modal_observers_) {
    DialogHostObserverRemoved();
  }
  for (auto& [profile, browser] : browsers_) {
    browser->GetTabStripModel()->RemoveObserver(this);
    if (auto* elements = BrowserElementsViews::From(browser)) {
      elements->TearDown();
    }
  }
  browsers_.clear();
  widget_close_allowed_ = true;
  if (widget_) {
    widget_->RemoveObserver(this);
  }
  widget_.reset();
}

NSWindow* WindowHost::ns_window() const {
  return widget_ ? widget_->GetNativeWindow().GetNativeNSWindow() : nil;
}

Browser* WindowHost::ExistingBrowserFor(Profile* profile) const {
  auto it = browsers_.find(profile);
  return it == browsers_.end() ? nullptr : it->second.get();
}

std::vector<Browser*> WindowHost::browsers() const {
  std::vector<Browser*> list;
  for (const auto& [profile, browser] : browsers_) {
    list.push_back(browser.get());
  }
  return list;
}

Browser* WindowHost::BrowserFor(Profile* profile) {
  if (!profile || profile->ShutdownStarted()) {
    return nullptr;
  }
  if (Browser* browser = ExistingBrowserFor(profile)) {
    return browser;
  }
  if (close_state_ != CloseState::kOpen || !widget_ ||
      widget_close_allowed_ || browser_shutdown::IsTryingToQuit() ||
      GetBrowserWindowCreationStatusForProfile(*profile) !=
          BrowserWindowInterface::CreationStatus::kOk) {
    return nullptr;
  }

  auto* window = new HostedBrowserWindow(GetWeakPtr());
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_NORMAL,
                                   *profile, /*user_gesture=*/true);
  params.window = window;
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  Browser* browser =
      static_cast<Browser*>(CreateBrowserWindow(std::move(params)));
  window->AttachBrowser(browser);
  AddBrowser(browser);
  return browser;
}

void WindowHost::AddBrowser(Browser* browser) {
  browsers_[browser->GetProfile()] = browser;
  if (auto* elements = BrowserElements::From(browser)) {
    if (auto* views_elements = elements->AsA<BrowserElementsViewsImpl>()) {
      views_elements->Init(widget_->GetRootView());
    }
  }
  browser->GetTabStripModel()->AddObserver(this);
  if (!active_profile_) {
    SetActiveProfile(browser->GetProfile());
  }
}

void WindowHost::SetActiveProfile(Profile* profile) {
  if (active_profile_ == profile) {
    return;
  }
  Browser* old_browser =
      active_profile_ ? ExistingBrowserFor(active_profile_) : nullptr;
  active_profile_ = profile;
  if (old_browser) {
    BrowserActiveStateManager::From(old_browser)->DidBecomeInactive();
  }
  Browser* browser = ExistingBrowserFor(profile);
  if (browser && widget_ && widget_->IsActive()) {
    ActivateExclusively(browser);
  }
}

bool WindowHost::IsActiveBrowser(const Browser* browser) const {
  return browser && active_profile_ &&
         ExistingBrowserFor(active_profile_) == browser;
}

void WindowHost::RequestActivation(Browser* browser) {
  if (!widget_ || !widget_->IsVisible() ||
      ExistingBrowserFor(browser->GetProfile()) != browser) {
    return;
  }
  if (IsActiveBrowser(browser)) {
    [ns_window() makeKeyAndOrderFront:nil];
  }
}

void WindowHost::OnWidgetActivationChanged(views::Widget* widget,
                                           bool active) {
  Browser* browser =
      active_profile_ ? ExistingBrowserFor(active_profile_) : nullptr;
  if (!browser) {
    return;
  }
  if (active) {
    ActivateExclusively(browser);
  } else {
    BrowserActiveStateManager::From(browser)->DidBecomeInactive();
  }
}

void WindowHost::OnWidgetBoundsChanged(views::Widget* widget,
                                       const gfx::Rect& new_bounds) {
  ScheduleDialogPositionUpdate();
}

// MARK: Closing (Ardia's sequence)

void WindowHost::CloseRequestedByUser() {
  if (close_state_ != CloseState::kOpen) {
    return;
  }
  PaneBrowserWindow* owner = owner_;
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;
  if ([delegate respondsToSelector:@selector(browserWindowShouldClose:)] &&
      ![delegate browserWindowShouldClose:owner]) {
    return;
  }
  Close();
}

void WindowHost::Close() {
  if (close_state_ != CloseState::kOpen) {
    return;
  }
  Trace("call", "PaneBrowserWindow.close");
  close_state_ = CloseState::kAsking;
  ++close_attempt_;
  ContinueClose();
}

void WindowHost::ContinueCloseAttempt(int attempt) {
  if (attempt != close_attempt_ || browser_shutdown::IsTryingToQuit()) {
    return;
  }
  ContinueClose();
}

void WindowHost::ContinueClose() {
  if (close_state_ != CloseState::kAsking) {
    return;
  }
  asking_ = nullptr;
  for (Browser* browser : browsers()) {
    if (UnloadController::From(browser)->RunBeforeUnloadForShutdown(
            false, base::BindRepeating(&WindowHost::OnBeforeUnloadAnswered,
                                       GetWeakPtr()))) {
      asking_ = browser;
      return;
    }
  }
  const int downloads = DownloadsCancelledByClose();
  if (downloads > 0) {
    ConfirmCloseWithDownloads(
        downloads, base::BindOnce(
                       [](base::WeakPtr<WindowHost> host, bool close_anyway) {
                         if (!host ||
                             host->close_state_ != CloseState::kAsking) {
                           return;
                         }
                         if (close_anyway) {
                           host->CommitClose();
                         } else {
                           host->CancelClose();
                         }
                       },
                       GetWeakPtr()));
    return;
  }
  CommitClose();
}

void WindowHost::OnBeforeUnloadAnswered(bool proceed) {
  if (close_state_ != CloseState::kAsking) {
    return;
  }
  asking_ = nullptr;
  if (proceed) {
    ContinueClose();
  } else {
    CancelClose();
  }
}

void WindowHost::CancelClose() {
  close_state_ = CloseState::kOpen;
  asking_ = nullptr;
  ++close_attempt_;
  for (Browser* browser : browsers()) {
    UnloadController::From(browser)->ResetBeforeUnloadForShutdown();
  }
}

void WindowHost::CommitClose() {
  close_state_ = CloseState::kClosing;
  PaneBrowserWindow* owner = owner_;
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;
  if ([delegate respondsToSelector:@selector(browserWindowWillClose:)]) {
    [delegate browserWindowWillClose:owner];
  }
  if (browsers_.empty()) {
    CloseWidget();
    return;
  }
  for (Browser* browser : browsers()) {
    browser->GetWindow()->Close();
  }
}

void WindowHost::CloseWidget() {
  if (widget_close_allowed_) {
    return;
  }
  widget_close_allowed_ = true;
  if (widget_) {
    widget_->Close();
  }
  std::erase(Hosts(), this);
  PaneBrowserWindow* owner = owner_ref_;
  owner_ref_ = nil;
  // Released on the next turn, outside the widget's own close call.
  dispatch_async(dispatch_get_main_queue(), ^{
    (void)owner;
  });
}

int WindowHost::DownloadsCancelledByClose() const {
  int count = 0;
  for (const auto& [profile, browser] : browsers_) {
    if (!profile->IsOffTheRecord()) {
      continue;
    }
    bool other_window = false;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* other) {
          if (other->GetProfile() == profile &&
              WindowHost::ForBrowser(other) != this &&
              !other->capabilities()->IsAttemptingToCloseBrowser()) {
            other_window = true;
          }
          return !other_window;
        });
    DownloadCoreService* downloads =
        DownloadCoreServiceFactory::GetForBrowserContext(profile);
    if (!other_window && downloads) {
      count += downloads->BlockingShutdownCount();
    }
  }
  return count;
}

void WindowHost::ConfirmCloseWithDownloads(
    int count,
    base::OnceCallback<void(bool)> callback) {
  // Pane's downloads are profile-wide and survive a window; only an
  // off-the-record profile's would be lost, and Pane has none yet.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

void WindowHost::BrowserWindowDestroyed(HostedBrowserWindow* window) {
  const bool was_asking = asking_ && window->browser() == asking_;
  if (was_asking) {
    asking_ = nullptr;
  }
  for (auto it = browsers_.begin(); it != browsers_.end(); ++it) {
    if (it->second == window->browser()) {
      it->second->GetTabStripModel()->RemoveObserver(this);
      if (active_profile_ == it->first) {
        active_profile_ = nullptr;
      }
      browsers_.erase(it);
      break;
    }
  }
  if (browsers_.empty() &&
      (closing() || browser_shutdown::IsTryingToQuit())) {
    close_state_ = CloseState::kClosing;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WindowHost::CloseWidget, GetWeakPtr()));
  } else if (was_asking && close_state_ == CloseState::kAsking &&
             !browser_shutdown::IsTryingToQuit()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WindowHost::ContinueCloseAttempt,
                                  GetWeakPtr(), close_attempt_));
  }
}

// MARK: Tabs

void WindowHost::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  base::AutoReset<int> notifying(&g_tab_strip_notifying,
                                 g_tab_strip_notifying + 1);
  PaneBrowserWindow* owner = owner_;
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;

  if (change.type() == TabStripModelChange::kInserted) {
    for (const auto& inserted : change.GetInsert()->contents) {
      TabBridge* bridge = TabBridge::GetOrCreate(inserted.contents);
      bridge->Observe();
      std::optional<WindowOpenDisposition> disposition =
          bridge->TakeOpenDisposition();
      content::WebContents* source = bridge->open_source();
      if (!disposition && pending_open_.disposition) {
        disposition = pending_open_.disposition;
        source = pending_open_.source.get();
      }
      if (!source) {
        tabs::TabInterface* opener_tab =
            tab_strip_model->GetOpenerOfTabAt(inserted.index);
        source = opener_tab ? opener_tab->GetContents() : nullptr;
      }
      const bool foreground =
          disposition
              ? *disposition != WindowOpenDisposition::NEW_BACKGROUND_TAB
              : tab_strip_model->active_index() == inserted.index;
      PaneWebContents* opener =
          source ? TabBridge::GetOrCreate(source)->tab() : nil;
      bridge->TraceCallback("PaneBrowserWindowDelegate.didInsertTab",
                            base::DictValue().Set("foreground", foreground));
      if ([delegate respondsToSelector:@selector
                    (browserWindow:didInsertTab:opener:foreground:)]) {
        [delegate browserWindow:owner
                   didInsertTab:bridge->tab()
                         opener:opener
                     foreground:foreground];
      }
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      TabBridge* bridge =
          removed.contents ? TabBridge::FromWebContents(removed.contents)
                           : nullptr;
      if (!bridge) {
        continue;
      }
      bridge->TraceCallback("PaneBrowserWindowDelegate.didRemoveTab");
      if ([delegate respondsToSelector:@selector(browserWindow:
                                                  didRemoveTab:)]) {
        [delegate browserWindow:owner didRemoveTab:bridge->tab()];
      }
    }
  }
}

void WindowHost::ActiveTabChanged(Browser* browser,
                                  content::WebContents* contents) {
  if (!contents) {
    return;
  }
  base::AutoReset<int> notifying(&g_tab_strip_notifying,
                                 g_tab_strip_notifying + 1);
  TabBridge* bridge = TabBridge::GetOrCreate(contents);
  bridge->TraceCallback("PaneBrowserWindowDelegate.didActivateTab");
  PaneBrowserWindow* owner = owner_;
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;
  if ([delegate respondsToSelector:@selector(browserWindow:didActivateTab:)]) {
    [delegate browserWindow:owner didActivateTab:bridge->tab()];
  }
}

void WindowHost::FullscreenChanged(content::WebContents* contents,
                                   bool fullscreen) {
  TabBridge* bridge = TabBridge::GetOrCreate(contents);
  bridge->TraceCallback("PaneBrowserWindowDelegate.didChangeFullscreen",
                        base::DictValue().Set("fullscreen", fullscreen));
  PaneBrowserWindow* owner = owner_;
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;
  if ([delegate respondsToSelector:@selector
                (browserWindow:tab:didChangeFullscreen:)]) {
    [delegate browserWindow:owner
                        tab:bridge->tab()
        didChangeFullscreen:fullscreen];
  }
}

// MARK: web_modal::WebContentsModalDialogHost (window-modal dialogs)

gfx::NativeView WindowHost::GetHostView() const {
  return gfx::NativeView(host_view_);
}

gfx::Point WindowHost::GetDialogPosition(const gfx::Size& size) {
  Browser* browser =
      active_profile_ ? ExistingBrowserFor(active_profile_) : nullptr;
  content::WebContents* contents =
      browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
  return DialogPositionInHostView(
      host_view_, PageRectInHostView(host_view_, contents), size);
}

bool WindowHost::ShouldActivateDialog() const {
  return WindowIsForeground(ns_window());
}

gfx::Size WindowHost::GetMaximumDialogSize() {
  Browser* browser =
      active_profile_ ? ExistingBrowserFor(active_profile_) : nullptr;
  content::WebContents* contents =
      browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
  return PageRectInHostView(host_view_, contents).size();
}

void WindowHost::AddObserver(web_modal::ModalDialogHostObserver* observer) {
  if (modal_observers_.HasObserver(observer)) {
    return;
  }
  modal_observers_.AddObserver(observer);
  DialogHostObserverAdded();
}

void WindowHost::RemoveObserver(web_modal::ModalDialogHostObserver* observer) {
  if (!modal_observers_.HasObserver(observer)) {
    return;
  }
  modal_observers_.RemoveObserver(observer);
  DialogHostObserverRemoved();
}

void WindowHost::NotifyPositionRequiresUpdate() {
  modal_observers_.Notify(
      &web_modal::ModalDialogHostObserver::OnPositionRequiresUpdate);
}

void WindowHost::ScheduleDialogPositionUpdate() {
  if (dialog_position_update_scheduled_) {
    return;
  }
  dialog_position_update_scheduled_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<WindowHost> host) {
                       if (!host) {
                         return;
                       }
                       host->dialog_position_update_scheduled_ = false;
                       host->NotifyPositionRequiresUpdate();
                     },
                     GetWeakPtr()));
}

// MARK: Hooks

namespace {

BrowserWindow* HostChromeBrowser(Browser* browser) {
  // Browsers Chrome makes itself (extension windows, popups it opens without a
  // tab) keep Chrome's own window for now (plan, Phase 4).
  return nullptr;
}

bool KeepsWindowWithoutTabs(const BrowserWindowInterface* browser) {
  WindowHost* host = WindowHost::ForBrowser(browser);
  return host && !host->closing();
}

std::unique_ptr<BrowserWebContentsDelegate> CreateWebContentsDelegate(
    BrowserWindowInterface* browser,
    ExclusiveAccessManager& exclusive_access_manager,
    chrome::BrowserCommandController& command_controller,
    UnloadController& unload_controller,
    web_app::AppBrowserController* app_browser_controller,
    BrowserWindow& window,
    DesktopBrowserWindowCapabilities& capabilities,
    BrowserUiController& browser_ui_controller) {
  HostedBrowserWindow* hosted = HostedBrowserWindow::FromWindow(&window);
  if (!hosted || !hosted->host_weak()) {
    return nullptr;
  }
  return std::make_unique<HostedWebContentsDelegate>(
      browser, exclusive_access_manager, command_controller,
      unload_controller, app_browser_controller, window, capabilities,
      browser_ui_controller, hosted->host_weak());
}

}  // namespace

void InstallBrowserHooks() {
  g_pane_browser_window_factory = &HostChromeBrowser;
  g_pane_keeps_window_without_tabs = &KeepsWindowWithoutTabs;
  g_pane_create_web_contents_delegate = &CreateWebContentsDelegate;
  g_pane_viewless_browsers = true;
}

}  // namespace pane
