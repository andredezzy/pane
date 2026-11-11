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
// change, so a page following the computer follows it.
void ChangeSystemTimeZone(const std::string& zone);

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

}  // namespace pane

#endif  // PANE_AUTOMATION_SYSTEM_H_
