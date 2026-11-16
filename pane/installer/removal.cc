// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/removal.h"

#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "pane/installer/files.h"
#include "pane/installer/switches.h"
#include "pane/installer/update_folder.h"

namespace pane::installer {

RemovalResult RemovePane(const base::FilePath& app,
                         bool unregistered,
                         RunUnregister unregister,
                         const FileRemoved& removed) {
  if (!IsInstalledAppFolder(app)) {
    LOG(ERROR) << "Not the install's folder, so not removed: " << app;
    return {.outcome = RemovalOutcome::kRefused,
            .error = ERROR_PATH_NOT_FOUND,
            .unregistered = unregistered};
  }
  if (!unregistered) {
    const std::optional<int> code = std::move(unregister).Run(app);
    if (code == kSwitchPaneOpen) {
      return {.outcome = RemovalOutcome::kPaneOpen};
    }
    if (code != kSwitchDone) {
      return {.outcome = RemovalOutcome::kNotUnregistered,
              .error = code ? static_cast<DWORD>(*code)
                            : static_cast<DWORD>(ERROR_GEN_FAILURE)};
    }
  }

  // The files, checked again: never a folder an earlier step let through.
  if (!IsInstalledAppFolder(app)) {
    return {.outcome = RemovalOutcome::kRefused,
            .error = ERROR_PATH_NOT_FOUND,
            .unregistered = true};
  }
  base::FileEnumerator files(app, /*recursive=*/true,
                             base::FileEnumerator::FILES);
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    const uint64_t size = files.GetInfo().GetSize();
    if (base::DeleteFile(file) && removed) {
      removed.Run(size);
    }
  }
  DWORD error = ERROR_SUCCESS;
  // And an update staged beside it, which no exit installs now
  // (update_folder.h).
  const bool app_gone = RemoveFolder(app, &error);
  const bool update_gone = RemoveFolder(UpdateFolderOf(app), &error);
  RemoveEmptyFolders();
  if (!app_gone || !update_gone) {
    return {.outcome = RemovalOutcome::kFilesLeft,
            .error = error,
            .unregistered = true};
  }
  return {.outcome = RemovalOutcome::kRemoved, .unregistered = true};
}

}  // namespace pane::installer
