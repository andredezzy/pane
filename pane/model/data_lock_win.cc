// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_lock.h"

#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"

namespace pane::model {

// static
std::unique_ptr<DataLock> DataLock::Take(const base::FilePath& data_directory) {
  base::CreateDirectory(data_directory);
  // Not inherited by a process Pane starts: base::File's handles never are.
  base::File file(data_directory.Append(FILE_PATH_LITERAL("Pane.lock")),
                  base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_READ |
                      base::File::FLAG_WRITE);
  if (!file.IsValid() ||
      file.Lock(base::File::LockMode::kExclusive) != base::File::FILE_OK) {
    return nullptr;
  }
  return base::WrapUnique(
      new DataLock(base::ScopedPlatformFile(file.TakePlatformFile())));
}

}  // namespace pane::model
