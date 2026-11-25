// Copyright 2026 Pane. Licensed under GPL-3.0-only.

// The Mac's system.h: what calls AppKit is in system_appkit_mac.mm.

#include "pane/automation/system.h"

#include <utility>

#include "base/path_service.h"
#include "chrome/browser/notifications/mac/notification_utils.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/notifications/notification_constants.h"
#include "chrome/common/notifications/notification_operation.h"
#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom.h"
#include "url/gurl.h"

namespace pane {

bool ClickSystemNotification(const std::string& profile_id,
                             const std::string& id,
                             const std::string& origin,
                             bool persistent) {
  auto info = mac_notifications::mojom::NotificationActionInfo::New();
  info->meta = mac_notifications::mojom::NotificationMetadata::New();
  info->meta->id = mac_notifications::mojom::NotificationIdentifier::New(
      id, mac_notifications::mojom::ProfileIdentifier::New(
              profile_id, /*incognito=*/false));
  info->meta->type = static_cast<int32_t>(
      persistent ? NotificationHandler::Type::WEB_PERSISTENT
                 : NotificationHandler::Type::WEB_NON_PERSISTENT);
  info->meta->origin_url = GURL(origin);
  // The browser's own, which the response must name to be heard
  // (VerifyMacNotificationData).
  info->meta->user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA).value();
  info->operation = NotificationOperation::kClick;
  info->button_index = kNotificationInvalidButtonIndex;
  ProcessMacNotificationResponse(mac_notifications::NotificationStyle::kBanner,
                                 std::move(info));
  return true;
}

}  // namespace pane
