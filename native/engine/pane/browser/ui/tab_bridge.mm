// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's TabBridge (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; native/third_party/ardia).

#include "pane/browser/ui/tab_bridge.h"

#include <utility>

#include "base/process/kill.h"
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
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<TabBridge>(*contents),
      tab_([[PaneWebContents alloc] initWithContents:contents]) {}

// Runs while the WebContents is being destroyed, after its observers heard
// WebContentsDestroyed (so web_contents() is null): GetWebContents() is still
// valid here.
TabBridge::~TabBridge() {
  if (observing_favicon_) {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(&GetWebContents())) {
      driver->RemoveObserver(this);
    }
  }
  if (observing_find_) {
    if (auto* finder =
            find_in_page::FindTabHelper::FromWebContents(&GetWebContents())) {
      finder->RemoveObserver(this);
    }
  }
  [tab_ contentsDestroyed];
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

void TabBridge::TraceCallback(std::string_view name, base::DictValue args) {
  args.Set("tab", tab_id());
  Trace("callback", name, std::move(args));
}

void TabBridge::TitleWasSet(content::NavigationEntry* entry) {
  [tab_ bridgeDidChangeTitle];
}

void TabBridge::PrimaryPageChanged(content::Page& page) {
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
  [tab_ bridgeRendererDidTerminate:termination];
}

void TabBridge::OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                                 NotificationIconType notification_icon_type,
                                 const GURL& icon_url,
                                 bool icon_url_changed,
                                 const gfx::Image& image) {
  if (notification_icon_type != NON_TOUCH_16_DIP || image.IsEmpty()) {
    return;
  }
  [tab_ bridgeDidUpdateFavicon:image.ToNSImage()];
}

void TabBridge::OnFindResultAvailable(content::WebContents* web_contents) {
  auto* finder = find_in_page::FindTabHelper::FromWebContents(web_contents);
  if (!finder) {
    return;
  }
  const find_in_page::FindNotificationDetails& result = finder->find_result();
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
