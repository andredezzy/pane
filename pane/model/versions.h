// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_VERSIONS_H_
#define PANE_MODEL_VERSIONS_H_

#include <string_view>

#include "base/types/expected.h"

namespace pane::model {

// A version with a part that isn't a number.
struct InvalidVersion {};

// Compares plain major.minor.patch versions. A leading "v" and any prerelease
// or build suffix are ignored: release tags for this repo are like "v0.1.1".
base::expected<bool, InvalidVersion> IsNewerVersion(std::string_view candidate,
                                                    std::string_view current);

}  // namespace pane::model

#endif  // PANE_MODEL_VERSIONS_H_
