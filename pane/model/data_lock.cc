// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_lock.h"

#include <utility>

#include "base/threading/platform_thread.h"

namespace pane::model {

DataLock::DataLock(base::ScopedPlatformFile file) : file_(std::move(file)) {}

DataLock::~DataLock() = default;

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
