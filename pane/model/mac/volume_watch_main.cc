// Copyright 2026 Pane. Licensed under GPL-3.0-only.

// pane-volume-watch <pid> <image>: waits for Pane's process to end, a crash
// included, then detaches the encrypted volume (ADR 0113), so the data never
// stays open without Pane. kqueue's NOTE_EXIT wakes it once, at the end:
// nothing polls. One serves a Pane for its whole life, through every lock: a
// lock detaches the image and the unlock attaches it again, on whichever
// device is free, so it is found by its path when the time comes. Pane ends it
// once a quit has detached the image (DataVault::EngineDidStop).

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/file.h>
#include <unistd.h>

#include <optional>

#include "base/at_exit.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "pane/model/mac/data_volume.h"

int main(int argc, char* argv[]) {
  base::AtExitManager at_exit;

  // SAFETY: the system gives `argc` arguments in `argv`.
  const base::span<char*> arguments =
      UNSAFE_BUFFERS(base::span(argv, static_cast<size_t>(argc)));
  int pid = 0;
  if (arguments.size() != 3 || !base::StringToInt(arguments[1], &pid) ||
      pid <= 0) {
    return 64;
  }

  // The data root is the image's folder, and the image is found by its path
  // there, as DataVolume names it.
  const base::FilePath image(arguments[2]);
  const pane::model::DataVolume volume(image.DirName(), std::nullopt);
  if (volume.image() != image) {
    return 64;
  }

  // A session of its own: a kill of Pane's process group, which ended Pane and
  // this watchdog together and left the volume mounted, or the hangup of the
  // terminal Pane was started from, does not reach it.
  signal(SIGHUP, SIG_IGN);
  setsid();

  const base::ScopedFD queue(kqueue());
  struct kevent change;
  EV_SET(&change, static_cast<uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT,
         NOTE_EXIT, 0, nullptr);
  // A process already gone fails to register (ESRCH): detach at once.
  if (queue.is_valid() &&
      kevent(queue.get(), &change, 1, nullptr, 0, nullptr) == 0) {
    struct kevent event;
    while (kevent(queue.get(), nullptr, 0, &event, 1, nullptr) == -1 &&
           errno == EINTR) {
    }
  }

  // The data's lock (DataLock), which Pane let go of as it ended: another Pane
  // that has taken it since holds the image now, and it stays. Held from here
  // to the end, so one that comes next waits for the detach. flock(2), as
  // every Pane takes it. A lock file that won't open leaves the detach to go
  // on: the data never stays open without Pane.
  const base::ScopedFD data_lock(
      HANDLE_EINTR(open(image.DirName().Append("Pane.lock").value().c_str(),
                        O_RDWR | O_CREAT | O_CLOEXEC, 0600)));
  if (data_lock.is_valid() &&
      HANDLE_EINTR(flock(data_lock.get(), LOCK_EX | LOCK_NB)) != 0) {
    return 0;
  }

  // Every attachment of the image, by its own device, which detaches it whole.
  volume.DetachLeftovers();
  return 0;
}
