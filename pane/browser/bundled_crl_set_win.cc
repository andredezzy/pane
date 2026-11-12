// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/bundled_crl_set.h"

#include "base/files/file_path.h"

namespace pane {

// Windows' package carries no CRLSet yet: it comes with its installer, W7
// (docs/plans/views-migration.md).
std::optional<base::FilePath> BundledCRLSet() {
  return std::nullopt;
}

}  // namespace pane
