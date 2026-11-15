// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/updates.h"

namespace pane::model {

// Windows' installer, Pane-<version>-x64.exe, which carries the app and
// installs and updates it (ADR 0127): never the Mac's dmg, which a release
// carries beside it.
const char kUpdateAssetSuffix[] = "-x64.exe";

}  // namespace pane::model
