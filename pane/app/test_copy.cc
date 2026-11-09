// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/test_copy.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"

namespace pane {

bool InTemporaryFolder(const base::FilePath& path) {
  base::FilePath temporary;
  if (!path.IsAbsolute() || path.ReferencesParent() ||
      !base::GetTempDir(&temporary)) {
    return false;
  }
  const base::FilePath folder = base::MakeAbsoluteFilePath(temporary);

  // What does not exist yet leads nowhere else; a link that leads nowhere
  // resolves to nothing, and so is refused.
  base::FilePath existing = path;
  while (!base::PathExists(existing) && !base::IsLink(existing)) {
    const base::FilePath parent = existing.DirName();
    if (parent == existing) {
      return false;
    }
    existing = parent;
  }
  const base::FilePath resolved = base::MakeAbsoluteFilePath(existing);

  return !folder.empty() && !resolved.empty() &&
         (folder.IsParent(resolved) ||
          (resolved == folder && existing != path));
}

}  // namespace pane
