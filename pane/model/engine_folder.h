// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_ENGINE_FOLDER_H_
#define PANE_MODEL_ENGINE_FOLDER_H_

#include "base/files/file_path.h"

namespace pane::model {

// The engine's folder in Pane's data folder: Chromium's user data dir, which
// holds Local State, the Default profile, and what the engine uses before the
// PIN, and where each of Pane's profiles has its folder by its id. The Mac's
// is Engine (ADR 0006). Windows' is User Data, the folder chrome_elf names for
// a launch with nothing on its command line, as the user's is, since Chromium
// takes its folder there from chrome_elf alone (ADR 0121,
// pane/app/data_launch_win.cc).
base::FilePath EngineFolder(const base::FilePath& data_directory);

}  // namespace pane::model

#endif  // PANE_MODEL_ENGINE_FOLDER_H_
