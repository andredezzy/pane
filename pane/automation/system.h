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

}  // namespace pane

#endif  // PANE_AUTOMATION_SYSTEM_H_
