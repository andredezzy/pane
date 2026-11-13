// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include "base/files/file_util.h"
#include "base/strings/string_util.h"

namespace pane::encryption {

std::vector<base::FilePath> RootsOf(const base::FilePath& user_data_dir) {
  std::vector<base::FilePath> roots = {user_data_dir};

  // As NTFS has it: long names for short ones, junctions followed, the case on
  // disk, from a handle on the folder. No volume: Windows' promise rests on
  // the PIN alone (docs/plans/views-migration.md, U5).
  base::FilePath normalized;

  if (base::NormalizeFilePath(user_data_dir, &normalized)) {
    roots.push_back(normalized);
  }

  return roots;
}

// NTFS folds every letter's case; ASCII's is folded here, where the drive, the
// user's folders, Engine, and every profile's id differ. A name differing in
// another letter's case reads as another form, which KeysFor refuses rather
// than writes plain. base::FilePath::CompareEqualIgnoreCase would fold them
// all, but it needs user32, which the Storage Service's sandbox (kService,
// with win32k locked down) doesn't have, and CHECKs it.
bool SameComponent(base::FilePath::StringViewType a,
                   base::FilePath::StringViewType b) {
  return base::EqualsCaseInsensitiveASCII(a, b);
}

}  // namespace pane::encryption
