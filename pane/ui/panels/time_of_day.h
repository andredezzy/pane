// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PANELS_TIME_OF_DAY_H_
#define PANE_UI_PANELS_TIME_OF_DAY_H_

#include <string>

namespace base {
class Time;
}

namespace pane {

// A time of day as the system writes it for the user, in the short style of
// their region's format, as the Swift's formatted(date: .omitted, time:
// .shortened) did: "17:44" where the region keeps 24 hours, "5:44 PM" where
// it keeps 12. Chromium's own (base::TimeFormatTimeOfDay) writes the app
// language's, which said "1:24 AM" here for "01:24".
std::u16string TimeOfDay(base::Time time);

}  // namespace pane

#endif  // PANE_UI_PANELS_TIME_OF_DAY_H_
