// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include "base/files/file_util.h"

namespace pane::encryption {

std::vector<base::FilePath> RootsOf(const base::FilePath& user_data_dir) {
  std::vector<base::FilePath> roots = {user_data_dir,
                                       user_data_dir.AppendASCII("Volume")};

  // Their resolved forms: /var is /private/var, and the caches' folder links
  // into the volume too. A realpath.
  const base::FilePath resolved = base::MakeAbsoluteFilePath(user_data_dir);

  if (!resolved.empty()) {
    roots.push_back(resolved);
    roots.push_back(resolved.AppendASCII("Volume"));
  }

  return roots;
}

bool SameComponent(base::FilePath::StringViewType a,
                   base::FilePath::StringViewType b) {
  return a == b;
}

}  // namespace pane::encryption
