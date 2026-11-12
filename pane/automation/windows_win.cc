// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/windows.h"

namespace pane {

// The process's windows, as a scenario reads them on Windows, are W6's
// (docs/plans/views-migration.md): none is listed yet, and no alert shows as a
// sheet (ShowSystemAlert).

base::ListValue NativeWindows(views::Widget* main) {
  return base::ListValue();
}

base::Value SheetAlert(views::Widget* main) {
  return base::Value();
}

bool PressSheetButton(views::Widget* main, std::string_view title) {
  return false;
}

base::Value ModalPanel() {
  return base::Value();
}

bool ResignKey(views::Widget* main) {
  return false;
}

}  // namespace pane
