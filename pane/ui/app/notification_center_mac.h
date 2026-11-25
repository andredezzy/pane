// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_NOTIFICATION_CENTER_MAC_H_
#define PANE_UI_APP_NOTIFICATION_CENTER_MAC_H_

// The Mac's way for a site's notification clicked in macOS's notification
// center into Pane: the hook patch 0004 defines in Chrome's
// notification_utils.cc, which includes this header (nogncheck), so a
// signature that drifts on either side is a compile error. Set (AppEvents,
// app_events_mac.cc), Chrome's handler hands each click, close, and settings
// item macOS's center gives it to Pane's UI (UiApp::NotificationClicked), as
// Windows' toasts come (notification_click.h): the profile opens through
// Pane's lock and its keys, once the PIN is in. Chrome's handler loaded the
// profile itself (ProfileManager::LoadProfile), outside both, and behind the
// lock its load never finished, the profile not coming back after the PIN
// (commit 9e50b3c). Unset, Chrome's handler goes on.

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom-forward.h"

extern void (*g_pane_notification_response)(
    mac_notifications::mojom::NotificationActionInfoPtr info);

// The profiles whose notifications Chrome's bridge closes as it is made, in
// both of macOS's centers, banners' and alerts', by their ids: the hook patch
// 0004 defines in Chrome's notification_platform_bridge_mac.cc, which
// includes this header too. Set before the initial profile's load, which
// makes the bridge (BrowserMainExtraParts::ClearLeftoverNotifications), it
// names this data's profiles, so a crash's notifications go before the PIN,
// and never another copy's. Unset, the bridge closes none.
extern std::vector<std::string> (*g_pane_notification_profile_ids)();

// The notifications of a profile, by its id, whose clicks Pane holds for its
// site until the profile has opened and hands them on (UiApp's
// HeldNotificationIds): the hook patch 0004 defines in Chrome's
// notification_platform_bridge_mac.cc too, which adds them to what it answers
// as shown. As a profile opens, its notification database drops each row the
// answer doesn't list (PlatformNotificationContextImpl's sync), and a click
// takes its notification out of macOS's center: when both centers' services
// had let go after a Lock, the answer came in the task that opened the
// profile, before the click's read, and the site never heard it. Set by the
// Mac's AppEvents. Unset, the bridge adds none.
extern std::set<std::string> (*g_pane_held_notification_ids)(
    const std::string& profile_id);

namespace pane {

struct NotificationClick;

// What a click macOS's center handed Chrome names, as Chrome's handler reads
// it (DoProcessMacNotificationResponse); nullopt for one that names no
// profile Pane has (an off-the-record one).
std::optional<NotificationClick> NotificationClickOf(
    const mac_notifications::mojom::NotificationActionInfo& info);

}  // namespace pane

#endif  // PANE_UI_APP_NOTIFICATION_CENTER_MAC_H_
