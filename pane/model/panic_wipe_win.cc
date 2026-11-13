// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe.h"

#include <windows.h>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"

namespace pane::model::wipe_files {

// The item's own attributes: a junction or a symbolic link is a reparse
// point, which goes without what it leads to.
base::expected<Kind, std::string> KindOf(const base::FilePath& path) {
  const DWORD attributes = ::GetFileAttributesW(path.value().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return Kind::kMissing;
    }
    return base::unexpected(LastError(path));
  }
  if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    return Kind::kOther;
  }
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    return Kind::kFolder;
  }
  return Kind::kFile;
}

// A reparse point is listed as what it is, and a listing never goes through
// one, since each folder is listed alone.
int ListedTypes() {
  return base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES;
}

// FlushFileBuffers: the bytes on the disk itself.
bool Sync(base::File& file) {
  return file.Flush();
}

// A rename on the same volume, as Wiping/<id>/ sits beside its target.
base::expected<void, std::string> Move(const base::FilePath& from,
                                       const base::FilePath& to) {
  if (base::PathExists(to)) {
    return base::unexpected(
        base::StrCat({to.AsUTF8Unsafe(), ": already exists"}));
  }
  if (!::MoveFileExW(from.value().c_str(), to.value().c_str(), 0)) {
    return base::unexpected(LastError(from));
  }
  return base::ok();
}

// A file, a reparse point, or a folder once empty: base::DeleteFile takes
// each, a junction without what it leads to.
base::expected<void, std::string> Remove(const base::FilePath& path) {
  if (!base::DeleteFile(path)) {
    return base::unexpected(LastError(path));
  }
  return base::ok();
}

std::string LastError(const base::FilePath& path) {
  return base::StrCat(
      {path.AsUTF8Unsafe(), ": ",
       logging::SystemErrorCodeToString(logging::GetLastSystemErrorCode())});
}

}  // namespace pane::model::wipe_files
