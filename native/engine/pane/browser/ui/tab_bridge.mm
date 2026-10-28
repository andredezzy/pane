// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's TabBridge (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; native/third_party/ardia).

#include "pane/browser/ui/tab_bridge.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/process/kill.h"
#include "base/task/single_thread_task_runner.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/find_in_page/find_notification_details.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/engine_trace.h"
#include "ui/gfx/image/image.h"

#import "pane/browser/web_contents_internal.h"

namespace pane {

TabBridge::TabBridge(content::WebContents* contents)
    : TabBridge(contents, [[PaneWebContents alloc] initWithContents:contents]) {
}

TabBridge::TabBridge(content::WebContents* contents, PaneWebContents* tab)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<TabBridge>(*contents),
      tab_(tab),
      pending_requests_([NSHashTable weakObjectsHashTable]) {}

TabBridge::~TabBridge() {
  [tab_ contentsDestroyed];
}

// The bridge stops observing the tab's helpers here, while they can still be
// looked up: by the time user data (the bridge included) is destroyed, the
// WebContents has already emptied its user data map.
void TabBridge::WebContentsDestroyed() {
  StopObservingHelpers();

  // The page is gone, and its frames with it: the app's object for it is
  // done, and so is every request it holds for the page that Chrome does
  // not take back itself. Nothing answers the page.
  [tab_ contentsDestroyed];
  ScopedAppCallback callback;
  JavaScriptDialogManager::GetInstance()->TabClosed(web_contents());
  [std::exchange(unresponsive_page_, nil) withdraw];
  for (PaneExternalProtocolRequest* request in pending_requests_.allObjects) {
    [request withdraw];
  }
  [pending_requests_ removeAllObjects];
}

void TabBridge::StopObservingHelpers() {
  if (observing_favicon_) {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      driver->RemoveObserver(this);
    }
    observing_favicon_ = false;
  }
  if (observing_find_) {
    if (auto* finder =
            find_in_page::FindTabHelper::FromWebContents(web_contents())) {
      finder->RemoveObserver(this);
    }
    observing_find_ = false;
  }
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
  if (!observing_find_) {
    if (auto* finder =
            find_in_page::FindTabHelper::FromWebContents(web_contents())) {
      finder->AddObserver(this);
      observing_find_ = true;
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

void TabBridge::WithdrawWhenClosed(PaneExternalProtocolRequest* request) {
  [pending_requests_ addObject:request];
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
  [tab_ bridgeDidChangeNavigationState];
}

void TabBridge::DidStartLoading() {
  [tab_ bridgeDidChangeLoading:YES progress:0];
}

void TabBridge::DidStopLoading() {
  [tab_ bridgeDidChangeLoading:NO progress:1];
}

void TabBridge::LoadProgressChanged(double progress) {
  [tab_ bridgeDidChangeLoading:web_contents()->IsLoading() progress:progress];
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
      FROM_HERE,
      base::BindOnce(
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
  if (old_view.superview && new_view && new_view != old_view) {
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
  if (web_contents()->GetVisibility() == content::Visibility::HIDDEN) {
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

void TabBridge::OnFindResultAvailable(content::WebContents* web_contents) {
  auto* finder = find_in_page::FindTabHelper::FromWebContents(web_contents);
  if (!finder) {
    return;
  }
  const find_in_page::FindNotificationDetails& result = finder->find_result();
  ScopedAppCallback callback;
  [tab_ bridgeDidFindMatches:result.number_of_matches()
                 activeMatch:result.active_match_ordinal()
                       final:result.final_update()];
}

void TabBridge::OnFindTabHelperDestroyed(find_in_page::FindTabHelper* helper) {
  helper->RemoveObserver(this);
  observing_find_ = false;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabBridge);

}  // namespace pane
