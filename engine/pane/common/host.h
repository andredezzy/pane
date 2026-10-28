// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_COMMON_HOST_H_
#define PANE_COMMON_HOST_H_

// The contract between the Pane app hosting the engine and the Chrome code
// patched to honor it. Header-only constants, so patched Chrome files can use
// them without depending on //pane.

// Set by PaneBrowserApplication when the Pane app hosts the engine. Chrome's
// own UI (startup windows, main menu, reopen windows) stays off under it.
#define PANE_HOSTED_SWITCH "pane-hosted"

// Posted by Chrome's AppController, instead of opening a window, when the user
// re-activates a hosted engine (for example from the Dock).
#define PANE_SHOULD_REOPEN_NOTIFICATION \
  "PaneBrowserApplicationShouldReopenNotification"
#define PANE_REOPEN_HAS_VISIBLE_WINDOWS_KEY "hasVisibleWindows"

namespace pane {

// Whether this process runs under the Pane app rather than Chrome's own
// executable.
bool IsHosted();

}  // namespace pane

#endif  // PANE_COMMON_HOST_H_
