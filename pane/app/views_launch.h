// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_VIEWS_LAUNCH_H_
#define PANE_APP_VIEWS_LAUNCH_H_

#include <optional>

namespace base {
class CommandLine;
}

namespace pane {

// Chromium's loader started the browser process: Pane's UI, in Views, runs in
// it. For now that launch runs only as a scenario's test copy, on the mock
// keychain and a data folder of its own in the temporary folder
// (test_copy.h), so it never opens the user's data; any other launch exits
// with the code this returns. Otherwise it brings the data up to date and
// takes its lock (PrepareData), which may end the launch too, then sets the
// engine's command line (SetEngineSwitches), and the browser starts Pane's
// UI.
std::optional<int> StartViewsLaunch(base::CommandLine& command_line);

// Whether this browser process got past StartViewsLaunch: its data is up
// and Pane's UI runs.
bool IsViewsLaunch();

}  // namespace pane

#endif  // PANE_APP_VIEWS_LAUNCH_H_
