// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_ENGINE_SWITCHES_H_
#define PANE_APP_ENGINE_SWITCHES_H_

#include "base/command_line.h"

namespace base {
class FilePath;
}  // namespace base

namespace pane {

// Makes `command_line` the one Pane's engine starts with. A launch on the
// mock keychain, a scenario's, whose secret opens none of the user's data
// (ADR 0113), keeps Chrome's switches. Any other keeps the URLs it opens and
// Pane's own switches alone: a process of the user's starting Pane with
// --remote-debugging-port, --enable-logging, or --load-extension would read
// every page once the PIN opened the data. Then come the switches every
// launch runs with: Chrome's own UI off, the features Pane keeps off, and
// the user data dir, `engine_folder` (model::EngineFolder), unless a
// scenario's command line names it.
void SetEngineSwitches(base::CommandLine& command_line,
                       const base::FilePath& engine_folder);

// A toast's activation on Windows (--notification-launch-id and its reply),
// which every launch keeps, so another launch hands it to the running Pane
// through the process singleton, as notification_helper.exe hands a running
// Chrome its toast. Chrome's startup reads it too, and opens the toast's
// profile at once, before Pane's lock and keys (CreateInitialProfile,
// GetStartupProfilePath): so the browser's own launch takes it out of its
// command line first (BrowserMainExtraParts::PreProfileInit), for Pane's UI,
// which hands it to the site once the profile is open (ADR 0109). Returns
// what it took, on a command line of its own.
base::CommandLine TakeNotificationSwitches(base::CommandLine& command_line);

}  // namespace pane

#endif  // PANE_APP_ENGINE_SWITCHES_H_
