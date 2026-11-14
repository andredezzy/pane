// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/focus_groups.h"

namespace pane {

// None on the Mac, where no button takes the keys but with full keyboard
// access, as AppKit's.
// static
std::unique_ptr<FocusGroups> FocusGroups::Make(RootView& root) {
  return nullptr;
}

// static
std::vector<ui::Accelerator> FocusGroups::Keys() {
  return {};
}

}  // namespace pane
