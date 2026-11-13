// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/file_panels.h"

#include <utility>

namespace pane {

// The stock panel's bridge cancels the panel as it goes
// (SelectFileDialogBridge's destructor), once its listener has.
void TakeDownStockPanel(gfx::NativeWindow owner) {}

// Gone with its bridge: the next starts at once.
void WhenStockPanelsEnded(gfx::NativeWindow owner, base::OnceClosure start) {
  std::move(start).Run();
}

}  // namespace pane
