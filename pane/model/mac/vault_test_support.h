// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_VAULT_TEST_SUPPORT_H_
#define PANE_MODEL_MAC_VAULT_TEST_SUPPORT_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/process/process.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "pane/model/mac/data_vault.h"
#include "pane/model/mac/data_volume.h"

namespace pane::model {

// One test at a time attaches a real image, across the test launcher's
// processes: a cleanup that detached by device number would take another
// test's image, given the same number a moment later. A lock on a file of its
// own in the temporary folder, held while the returned descriptor is.
base::ScopedFD LockImagesForTesting();

// Runs the test's own sequence until `condition` holds, for up to 30 s of
// real time: the thread pool works in real time, and a mock clock stays where
// it is, so a timer fires only where a test advances it.
[[nodiscard]] bool WaitFor(base::FunctionRef<bool()> condition);

// Runs the test's own sequence for `duration` of real time: for what must not
// happen meanwhile.
void RunFor(base::TimeDelta duration);

// Disk image tools in memory for the tests, as the Swift's scenarios faked
// them: an attach gives a device, an open makes the mount point a plain
// folder, so what the volume does to its folders runs as it is, and a detach
// gives the device up. Every call is recorded, in order, from whichever
// sequence made it.
class FakeVolumeTools : public VolumeTools {
 public:
  explicit FakeVolumeTools(const DataVolume& volume);

  // "attach-image", "attach-or-make", "open", "detach", "force-detach",
  // "watch", and "leftovers", in order. Holds is not recorded.
  std::vector<std::string> calls() const;
  int Count(std::string_view call) const;

  // The device the image is attached on, if it is.
  std::optional<std::string> attached() const;

  // From now, every open fails with `error`.
  void FailOpens(VolumeError error);

  // The next `count` detaches that aren't forced fail, as a volume whose
  // files are still open does.
  void FailDetaches(int count);

  // Each open waits for `gate` until it signals.
  void HoldOpens(base::WaitableEvent* gate);

  // Runs at each attach-or-make, before it attaches, on the volume's sequence.
  void SetOnAttach(base::RepeatingClosure on_attach);

  // Whether the watchdog it started has ended within `timeout`.
  bool WatchdogEnded(base::TimeDelta timeout);

  // VolumeTools:
  base::expected<DataVolume::Devices, VolumeError> AttachImageOnce() override;
  base::expected<DataVolume::Devices, VolumeError> AttachOrMake(
      std::string_view passphrase) override;
  base::expected<void, VolumeError> Open(const DataVolume::Devices& devices,
                                         std::string_view passphrase,
                                         std::string_view former) override;
  base::expected<void, VolumeError> Detach(std::string_view device,
                                           bool force) override;
  bool Holds(std::string_view device) override;
  base::expected<base::Process, VolumeError> Watch(
      const base::FilePath& helper) override;
  void DetachLeftovers() override;

 private:
  ~FakeVolumeTools() override;

  DataVolume::Devices Attach() EXCLUSIVE_LOCKS_REQUIRED(lock_);

  const DataVolume volume_;
  mutable base::Lock lock_;
  std::vector<std::string> calls_ GUARDED_BY(lock_);
  std::optional<std::string> attached_ GUARDED_BY(lock_);
  int next_disk_ GUARDED_BY(lock_) = 90;
  std::optional<VolumeError> open_failure_ GUARDED_BY(lock_);
  int detach_failures_ GUARDED_BY(lock_) = 0;
  raw_ptr<base::WaitableEvent> open_gate_ GUARDED_BY(lock_) = nullptr;
  base::RepeatingClosure on_attach_ GUARDED_BY(lock_);
  // A process of the test's own standing in for the watchdog.
  base::Process watchdog_ GUARDED_BY(lock_);
};

// The engine's conversion, restore, and repair for the tests: each records
// itself and how many ran at once for one profile, which must never be more
// than one, and a call can be held until the test lets it go.
class FakeFileMigrator : public base::RefCountedThreadSafe<FakeFileMigrator> {
 public:
  // A call held once it has begun, until Release.
  class Gate : public base::RefCountedThreadSafe<Gate> {
   public:
    Gate();

    // Blocks until the held call has begun.
    void WaitUntilHeld();
    void Release();

   private:
    friend class base::RefCountedThreadSafe<Gate>;
    friend class FakeFileMigrator;
    ~Gate();

    base::WaitableEvent held_;
    base::WaitableEvent released_;
  };

  FakeFileMigrator();

  DataVault::FileMigrator Get();

  // "migrate <id>", "restore <id>", "repair <id>", in the order they began.
  std::vector<std::string> calls() const;
  int Count(std::string_view call) const;
  // The most that ever ran at once for one profile.
  int most_at_once() const;

  // The next call of `kind` ("migrate", "restore", or "repair") for
  // `profile_id` waits, once it has begun, until the gate is released.
  scoped_refptr<Gate> Hold(std::string_view kind, std::string_view profile_id);
  // Lets every held call go, for the test's end.
  void ReleaseAll();

  void set_result(std::string_view kind, bool result);

 private:
  friend class base::RefCountedThreadSafe<FakeFileMigrator>;
  ~FakeFileMigrator();

  bool Run(std::string_view kind,
           const std::string& profile_id,
           base::RepeatingCallback<void(double)> progress);

  mutable base::Lock lock_;
  std::vector<std::string> calls_ GUARDED_BY(lock_);
  std::map<std::string, int> running_ GUARDED_BY(lock_);
  int most_at_once_ GUARDED_BY(lock_) = 0;
  std::map<std::string, bool, std::less<>> results_ GUARDED_BY(lock_);
  // By "<kind> <id>".
  std::map<std::string, scoped_refptr<Gate>, std::less<>> holds_
      GUARDED_BY(lock_);
  std::vector<scoped_refptr<Gate>> gates_ GUARDED_BY(lock_);
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_VAULT_TEST_SUPPORT_H_
