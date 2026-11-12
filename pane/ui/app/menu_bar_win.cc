// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/menu_bar.h"

namespace pane {

// Windows has no menu bar: a home for its commands, drawn in Paper first, is
// W5's (docs/plans/views-migration.md).
// static
std::unique_ptr<MenuBar::Native> MenuBar::Native::Show(ui::MenuModel& model) {
  return nullptr;
}

}  // namespace pane
