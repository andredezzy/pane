// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_NOTIFICATION_CENTER_MAC_H_
#define PANE_UI_APP_NOTIFICATION_CENTER_MAC_H_

// The Mac's way for a site's notification clicked in macOS's notification
// center into Pane: the hook patch 0004 defines in Chrome's
// notification_utils.cc, which includes this header (nogncheck), so a
// signature that drifts on either side is a compile error. Set (AppEvents,
// app_events_mac.mm), Chrome's handler hands each click, close, and settings
// item macOS's center gives it to Pane's UI (UiApp::NotificationClicked), as
// Windows' toasts come (notification_click.h): the profile opens through
// Pane's lock and its keys, once the PIN is in. Chrome's handler loaded the
// profile itself (ProfileManager::LoadProfile), outside both, and behind the
// lock its load never finished, the profile not coming back after the PIN
// (W8d). Unset, Chrome's handler goes on.

#include <optional>

#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom-forward.h"

extern void (*g_pane_notification_response)(
    mac_notifications::mojom::NotificationActionInfoPtr info);

namespace pane {

struct NotificationClick;

// What a click macOS's center handed Chrome names, as Chrome's handler reads
// it (DoProcessMacNotificationResponse); nullopt for one that names no
// profile Pane has (an off-the-record one).
std::optional<NotificationClick> NotificationClickOf(
    const mac_notifications::mojom::NotificationActionInfo& info);

}  // namespace pane

#endif  // PANE_UI_APP_NOTIFICATION_CENTER_MAC_H_
