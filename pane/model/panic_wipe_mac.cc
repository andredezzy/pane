// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/posix/eintr_wrapper.h"
#include "pane/model/mac/file_operations.h"

namespace pane::model::wipe_files {

// lstat(2): a link is itself, never what it leads to.
base::expected<Kind, std::string> KindOf(const base::FilePath& path) {
  base::stat_wrapper_t info;
  if (base::File::Lstat(path, &info) != 0) {
    if (errno == ENOENT) {
      return Kind::kMissing;
    }
    return base::unexpected(LastFileError(path));
  }
  if (S_ISDIR(info.st_mode)) {
    return Kind::kFolder;
  }
  if (S_ISREG(info.st_mode)) {
    return Kind::kFile;
  }
  return Kind::kOther;
}

int ListedTypes() {
  return base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
         base::FileEnumerator::SHOW_SYM_LINKS;
}

// As FileHandle.synchronize, fsync(2), where base::File::Flush asks only for a
// barrier.
bool Sync(base::File& file) {
  return HANDLE_EINTR(fsync(file.GetPlatformFile())) == 0;
}

base::expected<void, std::string> Move(const base::FilePath& from,
                                       const base::FilePath& to) {
  return MoveItem(from, to);
}

base::expected<void, std::string> Remove(const base::FilePath& path) {
  return RemoveItem(path);
}

std::string LastError(const base::FilePath& path) {
  return LastFileError(path);
}

}  // namespace pane::model::wipe_files
