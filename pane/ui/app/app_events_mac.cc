// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#include <set>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom.h"
#include "pane/ui/app/app_observers_mac.h"
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
    // Unretained: the observers go with this.
    hand_overs_ = HearHandOvers(
        base::BindRepeating(&AppEventsMac::Reopen, base::Unretained(this)),
        base::BindRepeating(&AppEventsMac::OpenURLsHanded,
                            base::Unretained(this)));
    // A process started from the app in front is made active as it finishes
    // launching (AppKit does so on this Mac), and kept the front for the
    // whole run. A scenario's copy that may not take it gives it back at once
    // to the app it took it from.
    if (!MayTakeFront()) {
      front_ = GiveTheFrontBack();
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
  }

 private:
  const HeldNotificationIds held_notification_ids_;
  // Declared in this order, so the hand-overs' observers go first.
  base::ScopedClosureRunner front_;
  base::ScopedClosureRunner hand_overs_;
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
