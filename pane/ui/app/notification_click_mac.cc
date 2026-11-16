// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/notification_click.h"

#include "base/command_line.h"
#include "chrome/browser/notifications/notification_platform_bridge.h"
#include "chrome/common/notifications/notification_constants.h"
#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom.h"
#include "pane/ui/app/notification_center_mac.h"

// The Mac's: macOS's notification center hands a click to Chrome's handler in
// the browser (NotificationDispatcherMojo), never as a launch, and Chrome's
// handler hands it to Pane (g_pane_notification_response, patch 0004).

namespace pane {

bool CarriesNotificationClick(const base::CommandLine& command_line) {
  return false;
}

std::optional<NotificationClick> NotificationClickOf(
    const base::CommandLine& command_line) {
  return std::nullopt;
}

// As DoProcessMacNotificationResponse reads it.
std::optional<NotificationClick> NotificationClickOf(
    const mac_notifications::mojom::NotificationActionInfo& info) {
  if (!info.meta || !info.meta->id || !info.meta->id->profile ||
      info.meta->id->profile->incognito) {
    return std::nullopt;
  }
  NotificationClick click;
  click.operation = info.operation;
  click.type = static_cast<NotificationHandler::Type>(info.meta->type);
  click.origin = info.meta->origin_url;
  click.notification_id = info.meta->id->id;
  click.profile_id =
      NotificationPlatformBridge::GetProfileBaseNameFromProfileId(
          info.meta->id->profile->id)
          .AsUTF8Unsafe();
  if (info.button_index != kNotificationInvalidButtonIndex) {
    click.action_index = info.button_index;
  }
  click.reply = info.reply;
  return click;
}

}  // namespace pane
