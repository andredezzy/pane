// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#import <AppKit/AppKit.h>

#include <set>
#include <string>
#include <utility>

#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom.h"
#include "net/base/apple/url_conversions.h"
#include "pane/common/host.h"
#include "pane/ui/app/notification_center_mac.h"

namespace pane {

namespace {

class AppEventsMac;

// The one that listens: Pane's UI has one.
AppEventsMac* g_mac_events = nullptr;

void HandToPane(mac_notifications::mojom::NotificationActionInfoPtr info);
std::set<std::string> HeldByPane(const std::string& profile_id);

class AppEventsMac : public AppEvents {
 public:
  AppEventsMac(base::RepeatingClosure reopen,
               OpenURLs open_urls,
               base::RepeatingClosure ask_to_exit,
               NotificationClicked notification_clicked,
               HeldNotificationIds held_notification_ids)
      : AppEvents(std::move(reopen),
                  std::move(open_urls),
                  std::move(ask_to_exit),
                  std::move(notification_clicked)),
        held_notification_ids_(std::move(held_notification_ids)) {
    g_mac_events = this;
    // Set for good: Chrome's own handler, which loads the profile outside
    // Pane's lock, never runs once Pane's UI has started, the clicks after it
    // has gone (a quit) going nowhere.
    g_pane_notification_response = &HandToPane;
    g_pane_held_notification_ids = &HeldByPane;
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
    // launching (AppKit does so on this Mac), and kept the front for the
    // whole run. A scenario's
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

  // A site's notification, clicked or closed in macOS's center: to Pane's
  // UI, which opens its profile through Pane's lock (UiApp's
  // NotificationClicked), as Windows' toasts come.
  void NotificationResponse(
      mac_notifications::mojom::NotificationActionInfoPtr info) {
    std::optional<NotificationClick> click = NotificationClickOf(*info);
    if (click) {
      NotificationLaunch(std::move(click));
    }
  }

  std::set<std::string> HeldNotifications(const std::string& profile_id) {
    return held_notification_ids_.Run(profile_id);
  }

  ~AppEventsMac() override {
    if (g_mac_events == this) {
      g_mac_events = nullptr;
    }
    [NSNotificationCenter.defaultCenter removeObserver:reopen_observer_];
    [NSNotificationCenter.defaultCenter removeObserver:open_urls_observer_];
    [NSNotificationCenter.defaultCenter removeObserver:became_active_];
    [NSWorkspace.sharedWorkspace.notificationCenter
        removeObserver:deactivated_];
  }

 private:
  const HeldNotificationIds held_notification_ids_;
  id __strong reopen_observer_;
  id __strong open_urls_observer_;
  id __strong became_active_;
  id __strong deactivated_;
};

void HandToPane(mac_notifications::mojom::NotificationActionInfoPtr info) {
  if (g_mac_events && info) {
    g_mac_events->NotificationResponse(std::move(info));
  }
}

std::set<std::string> HeldByPane(const std::string& profile_id) {
  return g_mac_events ? g_mac_events->HeldNotifications(profile_id)
                      : std::set<std::string>();
}

}  // namespace

bool ClosingTheWindowQuits() {
  return false;
}

// static
std::unique_ptr<AppEvents> AppEvents::Listen(
    base::RepeatingClosure reopen,
    OpenURLs open_urls,
    base::RepeatingClosure ask_to_exit,
    NotificationClicked notification_clicked,
    HeldNotificationIds held_notification_ids) {
  return std::make_unique<AppEventsMac>(
      std::move(reopen), std::move(open_urls), std::move(ask_to_exit),
      std::move(notification_clicked), std::move(held_notification_ids));
}

}  // namespace pane
