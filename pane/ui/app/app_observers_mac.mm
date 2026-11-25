// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_observers_mac.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "base/functional/bind.h"
#include "net/base/apple/url_conversions.h"
#include "pane/common/host.h"

namespace pane {

base::ScopedClosureRunner HearHandOvers(
    base::RepeatingClosure reopen,
    base::RepeatingCallback<void(std::vector<GURL>)> open_urls) {
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  id reopen_observer =
      [center addObserverForName:@PANE_SHOULD_REOPEN_NOTIFICATION
                          object:nil
                           queue:nil
                      usingBlock:^(NSNotification*) {
                        reopen.Run();
                      }];
  id open_urls_observer = [center
      addObserverForName:@PANE_OPEN_URLS_NOTIFICATION
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                std::vector<GURL> urls;
                for (NSURL* url in notification.userInfo[@PANE_OPEN_URLS_KEY]) {
                  urls.push_back(net::GURLWithNSURL(url));
                }
                open_urls.Run(std::move(urls));
              }];
  return base::ScopedClosureRunner(base::BindOnce(^{
    [NSNotificationCenter.defaultCenter removeObserver:reopen_observer];
    [NSNotificationCenter.defaultCenter removeObserver:open_urls_observer];
  }));
}

base::ScopedClosureRunner GiveTheFrontBack() {
  __block NSRunningApplication* previous =
      NSWorkspace.sharedWorkspace.frontmostApplication;
  NSRunningApplication* pane = NSRunningApplication.currentApplication;
  id deactivated = [NSWorkspace.sharedWorkspace.notificationCenter
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
  id became_active = [NSNotificationCenter.defaultCenter
      addObserverForName:NSApplicationDidBecomeActiveNotification
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
  return base::ScopedClosureRunner(base::BindOnce(^{
    [NSNotificationCenter.defaultCenter removeObserver:became_active];
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:deactivated];
  }));
}

}  // namespace pane
