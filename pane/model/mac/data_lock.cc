// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/data_lock.h"

#include <fcntl.h>
#include <sys/file.h>

#include <utility>

#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/platform_thread.h"

namespace pane::model {

DataLock::DataLock(base::ScopedFD descriptor)
    : descriptor_(std::move(descriptor)) {}

DataLock::~DataLock() = default;

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

// static
std::unique_ptr<DataLock> DataLock::Wait(const base::FilePath& data_directory,
                                         base::TimeDelta timeout) {
  // Monotonic, where the Swift read the wall clock: a clock set back would
  // have stretched the wait.
  const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  while (true) {
    if (std::unique_ptr<DataLock> lock = Take(data_directory)) {
      return lock;
    }
    if (base::TimeTicks::Now() > deadline) {
      return nullptr;
    }
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }
}

}  // namespace pane::model
