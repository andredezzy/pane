// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_CHROME_EXE_H_
#define PANE_INSTALLER_CHROME_EXE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/function_ref.h"
#include "base/process/launch.h"
#include "base/time/time.h"

namespace pane::installer {

// Pane's chrome.exe as the installer and the update's helper start it (ADR
// 0127): Pane itself, and its switches (switches.h), which register Pane
// with Windows, take it off, and start the uninstall
// (pane/app/install_switches_win.cc).

// How long registering may take, and the unregister that undoes a first
// install's.
inline constexpr base::TimeDelta kRegisterTimeout = base::Seconds(60);

// chrome.exe in an app folder laid out as an install lays it out.
base::FilePath ChromeIn(const base::FilePath& app);

// The folder a launch of Pane starts in: the user's own, as a shortcut's
// does, never the app folder, which a process's current folder would hold.
base::FilePath UserFolder();

// Runs `app`'s chrome.exe with `switches`, each "name" or "name=value",
// hidden, from UserFolder, and waits for it, `timeout` at most: its exit
// code, or nothing where it didn't start, or didn't end in time and was
// ended. Each step goes to `note`, the caller's notes.
std::optional<int> RunChrome(const base::FilePath& app,
                             const std::vector<std::string>& switches,
                             base::TimeDelta timeout,
                             base::FunctionRef<void(const std::string&)> note,
                             const base::LaunchOptions& extra = {});

// Registers Pane with Windows through `app`'s chrome.exe (kRegisterSwitch):
// "first" at a first install, "repair" at a reinstall and after an update's
// swap, kRegisterTimeout at most. Its exit code, kSwitchDone where it
// registered, or nothing, as RunChrome.
std::optional<int> RegisterWithWindows(
    const base::FilePath& app,
    bool first_install,
    base::FunctionRef<void(const std::string&)> note);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_CHROME_EXE_H_
