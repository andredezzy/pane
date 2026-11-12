// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

namespace pane {

// An update's file on Windows, its installer, is W7's
// (docs/plans/views-migration.md): none opens yet, and none leaves the Mark of
// the Web.

void OpenUpdateFile(const base::FilePath& file) {}

void OpenUpdateFileAfterExit(const base::FilePath& file) {}

bool OpenAfterProcessExits(int pid,
                           const base::FilePath& file,
                           const base::FilePath& opener) {
  return false;
}

bool ReleaseFromQuarantine(const base::FilePath& file) {
  return false;
}

}  // namespace pane
