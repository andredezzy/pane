// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_DATA_LAUNCH_H_
#define PANE_APP_DATA_LAUNCH_H_

#include <optional>

#include "base/values.h"

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace pane {

// Pane's data around the engine's life (docs/plans/views-migration.md, C5),
// in the Swift app's order (app/Pane/main.swift, AppDelegate): what an
// earlier Pane left is brought up to date, and the data's lock taken, before
// the engine starts; the vault and the lock open once the thread pool runs;
// and the volume detaches once the engine has stopped. The Mac's alone: the
// encrypted volume and every earlier Pane's data are there (ADR 0113, 0114,
// 0122), and no Pane ever ran on Windows (U5).

// Pane's data folder: a scenario's (PANE_DATA_DIR), or xyz.roundedcorners.pane
// in the user's Application Support (ADR 0114).
base::FilePath DataDirectory();

// Before the engine starts, from PaneMainDelegate::BasicStartupComplete, with
// no thread pool yet: Chromium's InitializeUserDataDir, right after, makes the
// engine's folder, and an earlier Pane's data must have moved by then. An exit
// code ends the launch there: the Quit of the Electron migration's alert.
std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line);

// Once the browser has started, the thread pool running: the vault and the
// lock, which opens the data key at once without a PIN, the volume made the
// first time, attached, and opened.
void OpenData();

// The lock, the wipe, and the vault, as the channel reports them
// (state.security).
base::DictValue DataState();

// Once the engine has stopped (PaneMainDelegate::ProcessExiting), its files
// closed: the volume detaches, and Local State names Default again.
void DataDidStop();

}  // namespace pane

#endif  // PANE_APP_DATA_LAUNCH_H_
