// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/last_version.h"

#include <string>

#include "base/files/file_util.h"
#include "base/strings/string_util.h"

namespace pane::model {

namespace {

// chrome/browser/downgrade/user_data_downgrade.cc's kDowngradeLastVersionFile.
constexpr base::FilePath::CharType kLastVersion[] =
    FILE_PATH_LITERAL("Last Version");

}  // namespace

bool KeepEngineVersion(const base::FilePath& engine_folder,
                       std::string_view version) {
  const base::FilePath file = engine_folder.Append(kLastVersion);
  std::string written;
  if (!base::ReadFileToString(file, &written)) {
    // None yet: Chromium processes nothing and writes its own.
    return !base::PathExists(file);
  }
  // Chromium reads it trimmed (GetLastVersion).
  if (base::TrimWhitespaceASCII(written, base::TRIM_ALL) == version) {
    return true;
  }
  return base::WriteFile(file, version);
}

}  // namespace pane::model
