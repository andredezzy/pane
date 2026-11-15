// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_LAST_VERSION_H_
#define PANE_MODEL_LAST_VERSION_H_

#include <string_view>

#include "base/files/file_path.h"

namespace pane::model {

// Chromium's `Last Version`, at the root of the engine's user data dir: the
// engine's version as it last ran there, which Chromium's downgrade processing
// reads at startup (DowngradeManager, chrome/browser/downgrade). On a managed
// device (base::IsEnterpriseDevice, or an enrolled browser) a new milestone
// copies Local State and the profiles Chromium names "Default" and "Profile N"
// into Snapshots; an older one moves the whole user data dir aside, profiles
// and all, restores a snapshot, and deletes what it moved at the next start; on
// Windows an administrator's downgrade version in the registry does the same
// without a snapshot. None of that knows Pane's profiles, whose folders are
// named by their ids and hold their own keys' files, so Pane never lets it run
// (ADR 0122): before the engine starts, the file names the engine's own
// version.

// Makes `engine_folder`'s `Last Version` say `version`, where it says another.
// No file, a first run, is left so. False when it would not write.
bool KeepEngineVersion(const base::FilePath& engine_folder,
                       std::string_view version);

}  // namespace pane::model

#endif  // PANE_MODEL_LAST_VERSION_H_
