// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_RUNNING_PANE_H_
#define PANE_INSTALLER_RUNNING_PANE_H_

#include <windows.h>

#include <vector>

#include "base/files/file_path.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"

namespace pane::installer {

// Every process running from `folder`, its executable in it: Pane's browser,
// its helpers, a notification helper. While one runs, the folder can't be
// replaced or removed. `except` is left out (the uninstall's chrome.exe that
// starts the dialog, on its way out).
std::vector<base::ProcessId> ProcessesFrom(
    const base::FilePath& folder,
    base::ProcessId except = base::kNullProcessId);

// The browsers among them: each one's process singleton window
// (Chrome_MessageWindow, chrome/browser/win/chrome_process_finder.cc), which
// another launch hands its command line to.
std::vector<HWND> PanesFrom(const base::FilePath& folder);

// Asks a running Pane to exit as Alt+F4 does, Exit Pane? first (Paper
// 11.12.7, ADR 0053): the command line `--pane-exit` handed over as another
// launch of Pane hands its own, so the window comes forward to ask. false
// where it didn't answer.
bool AskToExit(HWND pane);

// Ends what runs from the folder with no browser left to ask: a renderer or
// a helper that outlived its browser.
void EndProcesses(const std::vector<base::ProcessId>& processes);

// Until none of `processes` runs, or `timeout`: whether they all ended.
bool WaitForExit(const std::vector<base::ProcessId>& processes,
                 base::TimeDelta timeout);

// Until the Pane that opened the installer has gone, as Exit and install
// asks (Paper 11.12.11): its browser, `pid`, where that is a chrome.exe,
// for 2 minutes at most, then what ran from `folder`, its renderers and
// helpers, for 10 seconds at most. A Pane still there after is one the
// installer finds open, and says so.
void WaitForExitingPane(base::ProcessId pid, const base::FilePath& folder);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_RUNNING_PANE_H_
