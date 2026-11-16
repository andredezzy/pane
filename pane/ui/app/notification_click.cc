// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/notification_click.h"

#include "base/functional/callback_helpers.h"
#include "chrome/browser/notifications/notification_display_service_impl.h"

namespace pane {

void ForwardNotificationClick(Profile* profile,
                              const NotificationClick& click) {
  // A person's click, never one that looks scripted (by_user, and not
  // suspicious), as the system's center hands it to Chrome.
  NotificationDisplayServiceImpl::ProfileLoadedCallback(
      click.operation, click.type, click.origin, click.notification_id,
      click.action_index, click.reply, /*by_user=*/true,
      /*is_suspicious=*/false, base::DoNothing(), profile);
}

}  // namespace pane
