// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_VIEWS_LAUNCH_H_
#define PANE_APP_VIEWS_LAUNCH_H_

#include <optional>

namespace base {
class CommandLine;
}

namespace pane {

// Chromium's loader started the browser process, not the Swift app: Pane's UI
// is then the engine's own, in Views (docs/plans/views-migration.md). Until
// the data's port gives it the vault, that launch runs only as a scenario's
// test copy, on the mock keychain and a data folder of its own
// (PANE_DATA_DIR), so it never opens the user's data; any other launch exits
// with the code this returns. Otherwise it sets the engine's command line,
// as the Swift app's launch does, and the browser starts Pane's Views UI.
std::optional<int> StartViewsLaunch(base::CommandLine& command_line);

// Whether this browser process runs Pane's Views UI, from StartViewsLaunch.
bool IsViewsLaunch();

}  // namespace pane

#endif  // PANE_APP_VIEWS_LAUNCH_H_
