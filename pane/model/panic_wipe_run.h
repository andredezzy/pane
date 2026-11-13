// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PANIC_WIPE_RUN_H_
#define PANE_MODEL_PANIC_WIPE_RUN_H_

#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/process/process_handle.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "pane/model/panic_wipe.h"

namespace pane::model {

// The panic wipe's process around PanicWipe's erase (ADR 0042): the wiped
// instance's helpers, the instance it starts, and what that one does as it
// starts. The order is one on every system; each system's processes are its
// own (panic_wipe_run_mac.cc, panic_wipe_run_win.cc).

// Names the wiped instance and its helpers, for the one it starts to wait out.
inline constexpr char kWipeAfterSwitch[] = "pane-wipe-after";

// What a system's wipe takes besides the data's own files: on the Mac the
// caches the engine keeps apart, the Electron app's data and its key, the
// volume's leftovers, and the proxy passwords' store; on Windows the TPM's
// keys of the PIN (PinGuard).
struct WipeExtras {
  WipeExtras();
  WipeExtras(const WipeExtras&);
  WipeExtras& operator=(const WipeExtras&);
  ~WipeExtras();

  std::optional<base::FilePath> cache_directory;
  std::optional<ElectronData> electron;
  // At once, before the data moves aside.
  base::RepeatingClosure at_once;
  // Off the launch, once the moved data is erased: what may take its time.
  base::RepeatingClosure off_the_launch;
};

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
    const WipeExtras& extras);

// Whether security.json, or what is left of it, holds a PIN, the last failed
// attempt, and no key: a wipe the wiped instance never finished.
bool IsWipeDue(const base::FilePath& data_directory);

// This instance starts again, with its arguments and environment, and waits
// for `processes` to exit before it erases.
bool Relaunch(const std::vector<base::ProcessId>& processes);

// Only after the last attempt at the lock (AppLock), the count already on
// disk: this instance ends. Its helpers go first, so none (the network
// service, say) writes after, and the instance it starts erases once they are
// all gone (PanicWipeAtLaunch). With none started, this one erases, its engine
// still running. Never returns: no exit(), since nothing of the engine's
// shutdown may run, which would write.
[[noreturn]] void WipeAndRelaunch(const base::FilePath& data_directory,
                                  const WipeExtras& extras);

// Each system's own processes (panic_wipe_run_mac.cc, panic_wipe_run_win.cc).

// The engine's helpers: renderers, the GPU and network services, and
// Crashpad's handler where it runs.
std::vector<base::ProcessId> ChildProcesses();

// Ends each at once, nothing of theirs run.
void KillProcesses(const std::vector<base::ProcessId>& processes);

// Until every one has exited, or `timeout`.
void WaitForProcesses(const std::vector<base::ProcessId>& processes,
                      base::TimeDelta timeout);

// What this instance was started with, as the system gave it: the command
// line Chromium reads carries the switches Pane's launch adds to it
// (SetEngineSwitches, --pane-hosted among them), and an instance started with
// those runs as a hosted engine, never Pane's launch, which erases.
base::CommandLine::StringVector StartedWith();

// Starts `argv`, with this instance's environment.
bool Start(const base::CommandLine::StringVector& argv);

// Ends this instance at once.
[[noreturn]] void EndNow();

}  // namespace pane::model

#endif  // PANE_MODEL_PANIC_WIPE_RUN_H_
