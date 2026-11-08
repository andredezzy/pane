// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_ENGINE_SWITCHES_H_
#define PANE_APP_ENGINE_SWITCHES_H_

#include <string>
#include <vector>

namespace base {
class FilePath;
}

namespace pane {

// The command line Pane's engine starts with, from the one it was given
// (`argv`, the program first). A launch on the mock keychain, a scenario's,
// whose secret opens none of the user's data (ADR 0113), keeps Chrome's
// switches. Any other keeps the URLs it opens and Pane's own switches alone:
// a process of the user's starting Pane with --remote-debugging-port,
// --enable-logging, or --load-extension would read every page once the PIN
// opened the data. Then come the switches every launch runs with: Chrome's
// own UI off, the features Pane keeps off, and a user data dir in
// `data_directory`'s Engine, unless the command line names one.
std::vector<std::string> EngineCommandLine(
    const std::vector<std::string>& argv,
    const base::FilePath& data_directory);

}  // namespace pane

#endif  // PANE_APP_ENGINE_SWITCHES_H_
