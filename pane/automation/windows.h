// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_WINDOWS_H_
#define PANE_AUTOMATION_WINDOWS_H_

#include <string_view>

#include "base/values.h"

namespace views {
class Widget;
}

namespace pane {

// Every visible window of the process, for a scenario to tell Pane's from
// Chrome's showing through, as the Swift app's snapshot listed them: each
// one's role ("main", Pane's window, which `main` is, or null when closed;
// "sheet" on it, the system's file panel's accessory with it; "modal", an
// app-modal panel; "devtools", undocked DevTools; "popover", an action's
// popup;
// "system", AppKit's own full screen windows; "other"), its class, title,
// frame, and number.
base::ListValue NativeWindows(views::Widget* main);

// The alert the window shows as a sheet (the system's, ShowSystemAlert), as a
// scenario reads it: { title, message, buttons }, its buttons from left to
// right; none without one.
base::Value SheetAlert(views::Widget* main);

// Presses the sheet's button titled `title`, as a click on it does, or else
// the app-modal panel's (the system's print panel): false when neither has
// one.
bool PressSheetButton(views::Widget* main, std::string_view title);

// The app-modal panel showing (the system's print panel), as the Swift's
// snapshot read it: { title, buttons }; none without one.
base::Value ModalPanel();

}  // namespace pane

#endif  // PANE_AUTOMATION_WINDOWS_H_
