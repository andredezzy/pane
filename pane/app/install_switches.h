// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_INSTALL_SWITCHES_H_
#define PANE_APP_INSTALL_SWITCHES_H_

#include <optional>

namespace base {
class CommandLine;
}

namespace pane {

// What Pane's own install and uninstall ask of chrome.exe on Windows (ADR
// 0127; pane/installer/switches.h): registering with Windows, removing every
// registration and erasing the data on request, and the uninstall dialog's
// start. Before anything of Pane's launch: nothing of the data opens, and
// the engine doesn't start. nullopt where the command line has none, as on
// the Mac, which installs by the dmg's drag.
std::optional<int> RunInstallSwitch(const base::CommandLine& command_line);

// Pane's first window has painted: the install window that started this
// launch closes in that frame (Paper 11.12.6). Nothing where no installer
// waits.
void FirstWindowPainted();

}  // namespace pane

#endif  // PANE_APP_INSTALL_SWITCHES_H_
