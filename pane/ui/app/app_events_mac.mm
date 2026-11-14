// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "net/base/apple/url_conversions.h"
#include "pane/common/host.h"

namespace pane {

namespace {

class AppEventsMac : public AppEvents {
 public:
  AppEventsMac(base::RepeatingClosure reopen, OpenURLs open_urls)
      : AppEvents(std::move(reopen), std::move(open_urls)) {
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    // The block outlives no AppEventsMac: the observers go with it.
    AppEventsMac* events = this;
    reopen_observer_ =
        [center addObserverForName:@PANE_SHOULD_REOPEN_NOTIFICATION
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification*) {
                          events->Reopen();
                        }];
    open_urls_observer_ =
        [center addObserverForName:@PANE_OPEN_URLS_NOTIFICATION
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification* notification) {
                          std::vector<GURL> urls;
                          for (NSURL* url in notification
                                   .userInfo[@PANE_OPEN_URLS_KEY]) {
                            urls.push_back(net::GURLWithNSURL(url));
                          }
                          events->OpenURLsHanded(std::move(urls));
                        }];
    // A process started from the app in front is made active as it finishes
    // launching (AppKit does, on this Mac, for the published Swift app as
    // much as this one), and kept the front for the whole run. A scenario's
    // copy that may not take it gives it back at once to the app it took it
    // from: NSApp's deactivate leaves the app active since macOS 14, so the
    // other app is activated, as the active app may do.
    if (!MayTakeFront()) {
      __block NSRunningApplication* previous =
          NSWorkspace.sharedWorkspace.frontmostApplication;
      NSRunningApplication* pane = NSRunningApplication.currentApplication;
      deactivated_ = [NSWorkspace.sharedWorkspace.notificationCenter
          addObserverForName:NSWorkspaceDidDeactivateApplicationNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* notification) {
                    NSRunningApplication* app =
                        notification.userInfo[NSWorkspaceApplicationKey];
                    if (app && ![app isEqual:pane]) {
                      previous = app;
                    }
                  }];
      became_active_ =
          [center addObserverForName:NSApplicationDidBecomeActiveNotification
                              object:nil
                               queue:nil
                          usingBlock:^(NSNotification*) {
                            if (!previous || previous.terminated ||
                                [previous isEqual:pane]) {
                              return;
                            }
                            if (@available(macOS 14, *)) {
                              [NSApp yieldActivationToApplication:previous];
                              [previous activateFromApplication:pane options:0];
                            } else {
                              [previous activateWithOptions:0];
                            }
                          }];
    }
  }

  ~AppEventsMac() override {
    [NSNotificationCenter.defaultCenter removeObserver:reopen_observer_];
    [NSNotificationCenter.defaultCenter removeObserver:open_urls_observer_];
    [NSNotificationCenter.defaultCenter removeObserver:became_active_];
    [NSWorkspace.sharedWorkspace.notificationCenter
        removeObserver:deactivated_];
  }

 private:
  id __strong reopen_observer_;
  id __strong open_urls_observer_;
  id __strong became_active_;
  id __strong deactivated_;
};

}  // namespace

bool ClosingTheWindowQuits() {
  return false;
}

// static
std::unique_ptr<AppEvents> AppEvents::Listen(base::RepeatingClosure reopen,
                                             OpenURLs open_urls) {
  return std::make_unique<AppEventsMac>(std::move(reopen),
                                        std::move(open_urls));
}

}  // namespace pane
