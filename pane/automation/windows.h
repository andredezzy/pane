// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_WINDOWS_H_
#define PANE_AUTOMATION_WINDOWS_H_

#include "base/values.h"

namespace views {
class Widget;
}

namespace pane {

// Every visible window of the process, for a scenario to tell Pane's from
// Chrome's showing through, as the Swift app's snapshot listed them: each
// one's role ("main", Pane's window, which `main` is, or null when closed;
// "sheet" on it; "modal", an app-modal panel; "devtools", undocked DevTools;
// "system", AppKit's own full screen windows; "other"), its class, title, and
// frame.
base::ListValue NativeWindows(views::Widget* main);

}  // namespace pane

#endif  // PANE_AUTOMATION_WINDOWS_H_
