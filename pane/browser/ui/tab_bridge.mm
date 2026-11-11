// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's TabBridge (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; third_party/ardia).

#include "pane/browser/ui/tab_bridge.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/process/kill.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/app_callback.h"
#include "pane/browser/ui/window_host.h"
#import "pane/browser/web_contents_internal.h"
#include "pane/browser/web_store.h"
#include "ui/gfx/image/image.h"

namespace pane {

namespace {

// A tab of a window of Pane's Views UI, which follows its tabs into the page
// Chrome puts in a discarded one's place, and shows and loads that page
// itself (pane/ui/app/ui_app.cc): the bridge does so only for the Swift
// app's.
bool InViewsWindow(content::WebContents* contents) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  const WindowHost* host = browser ? WindowHost::ForBrowser(browser) : nullptr;
  return host && host->delegate();
}

}  // namespace

TabBridge::TabBridge(content::WebContents* contents)
    : TabBridge(contents, [[PaneWebContents alloc] initWithContents:contents]) {
}

TabBridge::TabBridge(content::WebContents* contents, PaneWebContents* tab)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<TabBridge>(*contents),
      tab_(tab) {}

TabBridge::~TabBridge() {
  [tab_ contentsDestroyed];
}

// The bridge stops observing the tab's helpers here, while they can still be
// looked up: by the time user data (the bridge included) is destroyed, the
// WebContents has already emptied its user data map.
void TabBridge::WebContentsDestroyed() {
  StopObservingHelpers();

  // The page is gone, and its frames with it: the app's object for it is
  // done. The page's dialogs go with it in the window's own
  // (pane/ui/dialogs).
  [tab_ contentsDestroyed];
}

void TabBridge::StopObservingHelpers() {
  if (observing_favicon_) {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      driver->RemoveObserver(this);
    }
    observing_favicon_ = false;
  }
}

PaneWebContents* TabFor(content::WebContents* web_contents) {
  TabBridge* bridge =
      web_contents ? TabBridge::FromWebContents(web_contents) : nullptr;
  return bridge ? bridge->tab() : nil;
}

TabBridge* TabBridge::GetOrCreate(content::WebContents* contents) {
  CreateForWebContents(contents);
  return FromWebContents(contents);
}

void TabBridge::Observe() {
  if (!observing_favicon_) {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      driver->AddObserver(this);
      observing_favicon_ = true;
    }
  }
}

void TabBridge::set_open(WindowOpenDisposition disposition,
                         content::WebContents* source) {
  open_disposition_ = disposition;
  open_source_ = source ? source->GetWeakPtr() : nullptr;
}

std::optional<WindowOpenDisposition> TabBridge::TakeOpenDisposition() {
  return std::exchange(open_disposition_, std::nullopt);
}

int TabBridge::tab_id() const {
  return sessions::SessionTabHelper::IdForTab(&GetWebContents()).id();
}

void TabBridge::TraceCallback(std::string_view name, base::DictValue args) {
  args.Set("tab", tab_id());
  Trace("callback", name, std::move(args));
}

void TabBridge::TitleWasSet(content::NavigationEntry* entry) {
  ScopedAppCallback callback;
  [tab_ bridgeDidChangeTitle];
}

void TabBridge::PrimaryPageChanged(content::Page& page) {
  ScopedAppCallback callback;
  [tab_ bridgeDidChangeURL];
  NavigationStateChanged();
}

void TabBridge::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  NavigationStateChanged();
}

void TabBridge::NavigationListPruned(
    const content::PrunedDetails& pruned_details) {
  NavigationStateChanged();
}

void TabBridge::NavigationEntriesDeleted() {
  NavigationStateChanged();
}

void TabBridge::NavigationStateChanged() {
  ScopedAppCallback callback;
  [tab_ bridgeDidChangeNavigationState];
}

void TabBridge::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  RenameWebStoreButtons(navigation_handle);
}

// The page is loading only while a new document loads in its main frame, as
// Chrome's own tab and toolbar show it (ShouldShowLoadingUI). A frame tree
// that loads only iframes, as an embed does when its tab comes forward or an
// ad as it refreshes, starts and stops loading too, under a page already
// shown. These three cover every change between the two: a main frame's load
// reports its progress as it starts and as it ends, each after its state
// changed, and the frame tree's start and stop cover the rest.
void TabBridge::DidStartLoading() {
  ReportLoading(0);
}

void TabBridge::DidStopLoading() {
  ReportLoading(1);
}

void TabBridge::LoadProgressChanged(double progress) {
  ReportLoading(progress);
}

void TabBridge::ReportLoading(double progress) {
  ScopedAppCallback callback;
  [tab_ bridgeDidChangeLoading:web_contents()->ShouldShowLoadingUI()
                      progress:progress];
}

void TabBridge::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  PaneRendererTermination termination = PaneRendererTerminationOther;
  switch (status) {
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
      termination = PaneRendererTerminationCrashed;
      break;
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
      termination = PaneRendererTerminationKilled;
      break;
    case base::TERMINATION_STATUS_OOM:
      termination = PaneRendererTerminationOutOfMemory;
      break;
    default:
      break;
  }
  // The app hears of it once the process's other observers have: an app that
  // reloads right away would otherwise relaunch the renderer mid-notification,
  // before extensions forgot the old process.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<TabBridge> bridge,
                        PaneRendererTermination termination) {
                       if (bridge) {
                         [bridge->tab_ bridgeRendererDidTerminate:termination];
                       }
                     },
                     weak_factory_.GetWeakPtr(), termination));
}

// Chrome discards a tab by putting an empty WebContents in its place, which
// loads the page again when shown; this one is destroyed right after (its
// requests are withdrawn then, as for a closed tab). The app's object moves
// to the new one before anything sees it, so every callback and call after
// this reaches the new page. A page the app is showing keeps its place: the
// new page's view takes the old one's.
void TabBridge::AboutToBeDiscarded(content::WebContents* new_contents) {
  if (!tab_ || FromWebContents(new_contents)) {
    return;
  }
  PaneWebContents* tab = std::exchange(tab_, nil);
  NSView* old_view = web_contents()->GetNativeView().GetNativeNSView();
  new_contents->SetUserData(UserDataKey(),
                            base::WrapUnique(new TabBridge(new_contents, tab)));
  [tab bridgeDidMoveToContents:new_contents];
  NSView* new_view = new_contents->GetNativeView().GetNativeNSView();
  if (old_view.superview && new_view && new_view != old_view &&
      !InViewsWindow(web_contents())) {
    new_view.frame = old_view.frame;
    new_view.autoresizingMask = old_view.autoresizingMask;
    new_view.hidden = old_view.hidden;
    [old_view.superview replaceSubview:old_view with:new_view];
  }
}

// The discard is over (the old page is gone). A page the app shows loads at
// once, as Chrome does for the selected tab of its focused window; one in
// the background waits until the app shows it (PaneWebContents -setVisible:).
void TabBridge::WasDiscarded() {
  if (web_contents()->GetVisibility() == content::Visibility::HIDDEN ||
      InViewsWindow(web_contents())) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<TabBridge> bridge) {
                       if (bridge) {
                         bridge->LoadIfDiscarded();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void TabBridge::LoadIfDiscarded() {
  if (!web_contents()->WasDiscarded()) {
    return;
  }
  PANE_TRACE_TAB(this, "PaneWebContents.reloadDiscarded");
  content::NavigationController& controller = web_contents()->GetController();
  controller.SetNeedsReload();
  controller.LoadIfNecessary();
}

void TabBridge::MediaPictureInPictureChanged(bool is_picture_in_picture) {
  ScopedAppCallback callback;
  [tab_ bridgeDidChangePictureInPicture:is_picture_in_picture];
}

void TabBridge::OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                                 NotificationIconType notification_icon_type,
                                 const GURL& icon_url,
                                 bool icon_url_changed,
                                 const gfx::Image& image) {
  if (notification_icon_type != NON_TOUCH_16_DIP || image.IsEmpty()) {
    return;
  }
  ScopedAppCallback callback;
  [tab_ bridgeDidUpdateFavicon:image.ToNSImage()];
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabBridge);

}  // namespace pane
