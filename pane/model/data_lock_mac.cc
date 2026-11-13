// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_lock.h"

#include <fcntl.h>
#include <sys/file.h>

#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/memory/ptr_util.h"
#include "base/posix/eintr_wrapper.h"

namespace pane::model {

// static
std::unique_ptr<DataLock> DataLock::Take(const base::FilePath& data_directory) {
  base::CreateDirectory(data_directory);
  base::ScopedFD descriptor(
      HANDLE_EINTR(open(data_directory.Append("Pane.lock").value().c_str(),
                        O_RDWR | O_CREAT | O_CLOEXEC, 0600)));
  if (!descriptor.is_valid() ||
      HANDLE_EINTR(flock(descriptor.get(), LOCK_EX | LOCK_NB)) != 0) {
    return nullptr;
  }
  return base::WrapUnique(new DataLock(std::move(descriptor)));
}

}  // namespace pane::model
