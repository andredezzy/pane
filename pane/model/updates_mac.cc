// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/updates.h"

namespace pane::model {

// The dmg every published Pane looks for in a release (ADR 0002), which
// scripts/dist.sh names Pane-<version>-arm64.dmg.
const char kUpdateAssetSuffix[] = "-arm64.dmg";

}  // namespace pane::model
