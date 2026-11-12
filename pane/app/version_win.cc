// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/version.h"

namespace pane {

// Windows' package carries no version of Pane's yet: it comes with its
// installer, W7 (docs/plans/views-migration.md).
std::optional<std::string> BundledPaneVersion() {
  return std::nullopt;
}

}  // namespace pane
