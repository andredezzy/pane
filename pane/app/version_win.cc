// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/version.h"

#include <string>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"

namespace pane {

namespace {

// More than a version's few bytes is not one of Pane's.
constexpr size_t kMaxVersionFile = 64;

}  // namespace

// The pane-version file beside chrome.dll (base::DIR_ASSETS), which
// scripts/package-win.sh writes from packaging/VERSION as package.sh writes
// the Mac's PaneVersion (ADR 0044). A build not packaged, out/Win's or a
// test copy's from it, has none.
std::optional<std::string> BundledPaneVersion() {
  base::FilePath assets;
  if (!base::PathService::Get(base::DIR_ASSETS, &assets)) {
    return std::nullopt;
  }
  std::string version;
  if (!base::ReadFileToStringWithMaxSize(
          assets.Append(FILE_PATH_LITERAL("pane-version")), &version,
          kMaxVersionFile)) {
    return std::nullopt;
  }
  version = std::string(base::TrimWhitespaceASCII(version, base::TRIM_ALL));
  if (version.empty()) {
    return std::nullopt;
  }
  return version;
}

}  // namespace pane
