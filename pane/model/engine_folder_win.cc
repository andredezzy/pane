// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/engine_folder.h"

namespace pane::model {

// Chromium's own name for it on Windows (chrome::kUserDataDirname): chrome_elf
// names %LOCALAPPDATA%\Pane\User Data for a launch with no --user-data-dir on
// the command line the process started with, the install mode's folder (patch
// 0001), and the data folder there is %LOCALAPPDATA%\Pane.
base::FilePath EngineFolder(const base::FilePath& data_directory) {
  return data_directory.Append(FILE_PATH_LITERAL("User Data"));
}

}  // namespace pane::model
