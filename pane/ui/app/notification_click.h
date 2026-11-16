// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_NOTIFICATION_CLICK_H_
#define PANE_UI_APP_NOTIFICATION_CLICK_H_

#include <optional>
#include <string>

#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/common/notifications/notification_operation.h"
#include "url/gurl.h"

class Profile;

namespace base {
class CommandLine;
}

namespace pane {

// A site's notification clicked, or closed, in the system's own center, and
// what Chrome's handler needs to hand it to the site: on Windows a toast's
// click, a button's, its close button's, or its settings item, which
// notification_helper.exe hands Chrome as a launch (`chrome.exe
// --notification-launch-id=…`), and which reaches the running Pane through
// the process singleton, or starts a closed one (ADR 0109).
struct NotificationClick {
  NotificationOperation operation = NotificationOperation::kClick;
  NotificationHandler::Type type = NotificationHandler::Type::WEB_PERSISTENT;
  GURL origin;
  std::string notification_id;
  // The profile's identifier, which names its folder in the engine's.
  std::string profile_id;
  std::optional<int> action_index;
  std::optional<std::u16string> reply;
};

// Whether a launch's command line carries a toast's activation at all: on
// Windows its --notification-launch-id, valid or not; never on the Mac, whose
// clicks reach Chrome's own handler in the browser (NotificationDispatcherMojo,
// from macOS's notification center).
bool CarriesNotificationClick(const base::CommandLine& command_line);

// The notification a launch's toast activation names, read as Chrome's own
// handler reads it (NotificationPlatformBridgeWin::HandleActivation); nullopt
// for one that names none, as a click on the app's own title in Windows'
// notification center does, or an off-the-record profile's, which Pane has
// none of.
std::optional<NotificationClick> NotificationClickOf(
    const base::CommandLine& command_line);

// Hands `click` to the site in `profile`, as Chrome's handler does once the
// profile it loaded is there (NotificationDisplayServiceImpl's
// ProfileLoadedCallback): the service worker's notificationclick, or the
// page's click, runs, as do a button's, the close, and the settings item.
void ForwardNotificationClick(Profile* profile, const NotificationClick& click);

}  // namespace pane

#endif  // PANE_UI_APP_NOTIFICATION_CLICK_H_
