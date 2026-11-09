// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_PANIC_WIPE_RUN_H_
#define PANE_MODEL_MAC_PANIC_WIPE_RUN_H_

#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/process/process_handle.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_annotations.h"
#include "pane/model/mac/panic_wipe.h"
#include "pane/model/mac/safe_storage.h"

namespace pane::model {

class VolumeTools;

// The panic wipe's process around PanicWipe's erase (ADR 0042): the wiped
// instance's helpers, the instance it starts, and what that one does as it
// starts.

// Names the wiped instance and its helpers, for the one it starts to wait out.
inline constexpr char kWipeAfterSwitch[] = "pane-wipe-after";

// Where the engine keeps the profiles' caches when apart from their data:
// under ~/Library/Caches, for a data root under ~/Library/Application Support
// (CacheDirectoryFor). nullopt anywhere else.
std::optional<base::FilePath> ProfileCacheDirectory(
    const base::FilePath& data_directory);

// The Electron app's data this Pane migrated from, which the wipe erases too
// (ElectronData).
std::optional<ElectronData> ElectronDataFor(
    const base::FilePath& data_directory);

// The erase at launch, on a thread of its own, and what it could not erase:
// for the security scenario.
class LaunchErase : public base::RefCountedThreadSafe<LaunchErase> {
 public:
  LaunchErase();
  LaunchErase(const LaunchErase&) = delete;
  LaunchErase& operator=(const LaunchErase&) = delete;

  bool IsErasing() const;
  std::vector<std::string> Failures() const;

  // Blocks until the erase ends.
  void Wait();

  // The erase's thread, once it ends.
  void Finish(std::vector<std::string> failures);

 private:
  friend class base::RefCountedThreadSafe<LaunchErase>;
  ~LaunchErase();

  mutable base::Lock lock_;
  bool erasing_ GUARDED_BY(lock_) = true;
  std::vector<std::string> failures_ GUARDED_BY(lock_);
  base::WaitableEvent done_;
};

// At launch, before anything reads the data directory, and before the thread
// pool exists (docs/plans/views-migration.md, C5). The wiped instance's switch
// names it and its helpers, which this waits out. A wipe is due too when
// security.json still holds the last attempt: the wiped instance died before
// starting this one. The data moves out of the way at once, so Pane starts
// fresh, and is erased on a thread; data a wipe cut short left (a crash, a
// quit) is erased the same way. Null when nothing is staged.
scoped_refptr<LaunchErase> PanicWipeAtLaunch(
    const base::FilePath& data_directory,
    const base::CommandLine& command_line,
    VolumeTools& tools,
    SafeStorage safe_storage);

// Whether security.json, or what is left of it, holds a PIN, the last failed
// attempt, and no key: a wipe the wiped instance never finished.
bool IsWipeDue(const base::FilePath& data_directory);

// The Electron app's cookie key goes with its data, when macOS lets it go
// without asking: a prompt would ask whoever typed the wrong PINs. Its data
// erased, the key opens nothing anyway.
void ForgetElectronKey(const std::optional<ElectronData>& electron,
                       SafeStorage safe_storage);

// The engine's helpers: renderers, the GPU and network services, and
// Crashpad's handler.
std::vector<base::ProcessId> ChildProcesses();

// This instance starts again, with its arguments and environment, and waits
// for `processes` to exit before it erases.
bool Relaunch(const std::vector<base::ProcessId>& processes);

// Only after the last attempt at the lock (AppLock), the count already on
// disk: this instance ends. Its helpers go first, so none (the network
// service, say) writes after, and the instance it starts erases once they are
// all gone (PanicWipeAtLaunch). With none started, this one erases, its engine
// still running. Never returns: not exit(), since nothing of the engine's
// shutdown may run, which would write.
[[noreturn]] void WipeAndRelaunch(const base::FilePath& data_directory,
                                  VolumeTools& tools,
                                  SafeStorage safe_storage);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_PANIC_WIPE_RUN_H_
