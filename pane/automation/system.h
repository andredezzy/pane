// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_SYSTEM_H_
#define PANE_AUTOMATION_SYSTEM_H_

#include <optional>
#include <string>

#include "base/types/expected.h"

namespace pane {

// The computer's time zone changes to `zone` (IANA's name), as far as the
// engine can tell, for a scenario (fingerprint): the zone the platform's
// time zone monitor reads becomes `zone`, and the platform tells it of the
// change, so a page following the computer follows it. False where the
// engine reads the computer's zone alone (Windows), which no scenario
// changes.
bool ChangeSystemTimeZone(const std::string& zone);

// What an assistive app sets on the application to ask for its whole
// accessibility tree (AXEnhancedUserInterface), which Chromium answers on a
// timer (data-volume).
void RequestEnhancedUserInterface();

// What the system tells apps that locks Pane by itself, posted to Pane alone,
// as the system posts it: "screensSleep" (the displays sleeping) or
// "sessionResign" (another user taking the session). False for another.
bool PostSystemEvent(const std::string& event);

// `source` run as AppleScript in the app, against itself (`tell current
// application`, with the dictionary's raw codes), as a launcher reaches a
// browser from outside, but asking the Mac's user for no Apple Events
// permission (applescript, ADR 0112): its result as text, none when it gives
// none, or what went wrong.
base::expected<std::optional<std::string>, std::string> RunAppleScript(
    const std::string& source);

// A site's notification clicked in macOS's notification center, handed to
// the browser as the center hands it (ProcessMacNotificationResponse, which
// NotificationDispatcherMojo calls for a banner's click), with no click on
// the user's screen: `profile_id` the profile's folder's name, `id` Chrome's
// notification id, `origin` the site's, `persistent` a service worker's. False
// on Windows, where a toast's click comes as another launch, as the runner
// hands it (notification-click).
bool ClickSystemNotification(const std::string& profile_id,
                             const std::string& id,
                             const std::string& origin,
                             bool persistent);

}  // namespace pane

#endif  // PANE_AUTOMATION_SYSTEM_H_
