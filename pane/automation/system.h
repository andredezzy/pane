// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_SYSTEM_H_
#define PANE_AUTOMATION_SYSTEM_H_

#include <string>

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

}  // namespace pane

#endif  // PANE_AUTOMATION_SYSTEM_H_
