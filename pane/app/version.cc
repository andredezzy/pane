// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/version.h"

namespace pane {

std::string PaneVersion() {
  return BundledPaneVersion().value_or("0.0.0");
}

}  // namespace pane
