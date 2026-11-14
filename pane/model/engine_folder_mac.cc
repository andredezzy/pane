// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/engine_folder.h"

namespace pane::model {

// Pane names it, with --user-data-dir (SetEngineSwitches), which Chromium
// reads from its own command line on the Mac.
base::FilePath EngineFolder(const base::FilePath& data_directory) {
  return data_directory.Append("Engine");
}

}  // namespace pane::model
