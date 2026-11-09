// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_DATA_LOCK_H_
#define PANE_MODEL_MAC_DATA_LOCK_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/time/time.h"

namespace pane::model {

// Held by the one Pane that runs on a data root (ADR 0113): the image's
// attach, its watchdog, and its detach are that Pane's alone. Another one
// started on the same data, which hands its URLs to the first through the
// engine's singleton and quits, finds it held and leaves the image alone; one
// started while the last quits waits for it. A lock on a file, which the
// system lets go of with its process, a crash included, and which the
// processes Pane starts do not inherit (the update's relaunch, the watchdog).
//
// It is flock(2) on Pane.lock, as every published Pane and its watchdog take
// it, not base::File::Lock, which is fcntl's and would not keep them out.
class DataLock {
 public:
  DataLock(const DataLock&) = delete;
  DataLock& operator=(const DataLock&) = delete;
  // Lets go.
  ~DataLock();

  // Null while another process holds it.
  static std::unique_ptr<DataLock> Take(const base::FilePath& data_directory);

  // Once the process that holds it lets go, which a Pane quitting does after
  // it detaches the image. Null past the timeout. Blocks, so where waiting is
  // allowed.
  static std::unique_ptr<DataLock> Wait(const base::FilePath& data_directory,
                                        base::TimeDelta timeout);

 private:
  explicit DataLock(base::ScopedFD descriptor);

  base::ScopedFD descriptor_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_DATA_LOCK_H_
