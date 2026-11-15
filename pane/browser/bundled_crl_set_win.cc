// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/bundled_crl_set.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"

namespace pane {

// The crl-set file beside chrome.dll (base::DIR_ASSETS), which
// scripts/package-win.sh fetches into the package as package.sh puts the
// Mac's in the bundle. A build not packaged, out/Win's or a test copy's from
// it, has none.
std::optional<base::FilePath> BundledCRLSet() {
  base::FilePath assets;
  if (!base::PathService::Get(base::DIR_ASSETS, &assets)) {
    return std::nullopt;
  }
  base::FilePath crl_set = assets.Append(FILE_PATH_LITERAL("crl-set"));
  if (!base::PathExists(crl_set)) {
    return std::nullopt;
  }
  return crl_set;
}

}  // namespace pane
