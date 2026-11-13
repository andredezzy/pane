// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_VOLUME_PLACE_H_
#define PANE_MODEL_MAC_VOLUME_PLACE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/ref_counted.h"
#include "base/process/process.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "pane/model/data_key.h"
#include "pane/model/data_place.h"
#include "pane/model/mac/data_volume.h"

namespace pane::model {

class DataLock;

// What the vault asks of the disk image tools: DataVolume's attach, unlock,
// mount, and detach, which run hdiutil and diskutil, and mount through Disk
// Arbitration. A seam the tests fake, as the Swift's scenarios did; what the
// volume does to its folders runs as it is. Called on the volume's sequence,
// at launch on a thread of its own, and at exit on the main thread, so a fake
// is thread-safe.
class VolumeTools : public base::RefCountedThreadSafe<VolumeTools> {
 public:
  // DataVolume's own tools, for `volume`.
  static scoped_refptr<VolumeTools> For(const DataVolume& volume);

  VolumeTools(const VolumeTools&) = delete;
  VolumeTools& operator=(const VolumeTools&) = delete;

  virtual base::expected<DataVolume::Devices, VolumeError>
  AttachImageOnce() = 0;
  virtual base::expected<DataVolume::Devices, VolumeError> AttachOrMake(
      std::string_view passphrase) = 0;
  virtual base::expected<void, VolumeError> Open(
      const DataVolume::Devices& devices,
      std::string_view passphrase,
      std::string_view former) = 0;
  virtual base::expected<void, VolumeError> Detach(std::string_view device,
                                                   bool force) = 0;
  virtual bool Holds(std::string_view device) = 0;
  virtual base::expected<base::Process, VolumeError> Watch(
      const base::FilePath& helper) = 0;
  virtual void DetachLeftovers() = 0;

 protected:
  friend class base::RefCountedThreadSafe<VolumeTools>;

  VolumeTools() = default;
  virtual ~VolumeTools() = default;
};

// The image's attach as Pane starts, its volume locked, while the engine
// starts and the PIN is typed (ADR 0113), with its watchdog: what is left once
// the key opens is the unlock and the mount. It runs before the thread pool
// exists (docs/plans/views-migration.md, C5), on a thread of its own, and only
// in the Pane that holds the data's lock: a Pane started while another runs
// leaves that one's image alone. The vault takes what it came to.
class LaunchAttach : public base::RefCountedThreadSafe<LaunchAttach> {
 public:
  // Starts it, when the image exists: a fresh install has none until the key
  // first opens.
  static scoped_refptr<LaunchAttach> Start(const DataVolume& volume,
                                           scoped_refptr<VolumeTools> tools,
                                           const base::FilePath& watchdog);

  LaunchAttach(const LaunchAttach&) = delete;
  LaunchAttach& operator=(const LaunchAttach&) = delete;

  // Whether it ended within `timeout`.
  bool Wait(base::TimeDelta timeout);

  // Once it has ended: the devices it attached, none when it failed. One
  // that fails here is attached again, leftovers cleared, once the key opens.
  std::optional<DataVolume::Devices> devices() const;

  // The watchdog it started, once.
  base::Process TakeWatchdog();

 private:
  friend class base::RefCountedThreadSafe<LaunchAttach>;
  class Thread;

  LaunchAttach();
  ~LaunchAttach();

  base::WaitableEvent done_;
  // Written before `done_` signals, read after.
  std::optional<DataVolume::Devices> devices_;
  base::Process watchdog_;
};

// The Mac's place for every profile's engine data (ADR 0113): the encrypted
// volume, attached once the key opens, an earlier Pane's plain folders moved
// in, each profile's folders and links made, its safety copy restored, its
// files converted and repaired (ADR 0121, 0122), and the volume detached at a
// lock and at quit. What the vault's data sequence did to the volume before
// Windows' place joined it (folder_place.h), unchanged. What is attached, the
// launch's attach, and the watchdog are kept under `lock_`, which the close at
// exit reads while a step that outlived the thread pool may still run
// (CONTINUE_ON_SHUTDOWN).
class VolumePlace : public DataPlace {
 public:
  // Between attempts to detach: at a lock, with the engine running, never
  // forced, 10 s at most, since a profile's stores close a moment after its
  // context does; at exit, a moment for the engine's helpers (the network
  // service keeps the cookies open), then forced. The tests shorten them.
  struct DetachWaits {
    DetachWaits();
    DetachWaits(const DetachWaits&);
    DetachWaits& operator=(const DetachWaits&);
    ~DetachWaits();

    std::vector<base::TimeDelta> at_lock;
    std::vector<base::TimeDelta> at_exit;
  };

  // Where the watchdog is, beside Pane's executable (the app's Contents/MacOS,
  // beside the loader).
  static base::FilePath WatchdogPath();

  // `data_lock`, held from launch, or null when another Pane held it then:
  // this one waits for it at its first attach. `launch`, the image's attach at
  // launch, or null.
  VolumePlace(const base::FilePath& data_directory,
              std::optional<base::FilePath> cache_directory,
              scoped_refptr<VolumeTools> tools,
              std::unique_ptr<DataLock> data_lock,
              scoped_refptr<LaunchAttach> launch,
              DetachWaits waits = DetachWaits());

  const DataVolume& volume() const { return volume_; }
  VolumeTools& tools() const { return *tools_; }

  // DataPlace:
  void Attach(const Key& key) override;
  void Finish(std::optional<std::vector<std::string>> moving,
              base::RepeatingCallback<void(double)> moved,
              base::OnceCallback<void(AttachResult)> reply) override;
  void CloseAtLock(bool every_closed,
                   base::OnceCallback<void(Closed)> reply) override;
  void CloseAtExit() override;
  bool HasEarlierData(const std::vector<std::string>& ids) const override;
  bool BringUpToDate(const ProfileWork& work) override;
  void ProfileCameUp(const std::string& profile_id) override;
  bool Remove(const std::string& profile_id) override;

 private:
  class Busy;

  ~VolumePlace() override;

  base::expected<AttachTimes, std::string> AttachOnce(const Key& key);
  std::optional<DataVolume::Devices> TakeLaunch(base::TimeDelta wait);
  void Watch();
  std::optional<std::string> DeviceToDetach(base::TimeDelta launch_wait,
                                            bool check_known);
  bool TryDetaching(const std::string& device,
                    const std::vector<base::TimeDelta>& waits);
  bool Repair(const ProfileWork& work) const;
  bool EncryptFiles(const ProfileWork& work) const;

  const DataVolume volume_;
  const base::FilePath data_directory_;
  const std::optional<base::FilePath> cache_directory_;
  const scoped_refptr<VolumeTools> tools_;
  const base::FilePath watchdog_path_;
  const DetachWaits waits_;

  // On the data's sequence: the data's lock, once this Pane holds it, and
  // the last attach's outcome, which the open's Finish reports.
  std::unique_ptr<DataLock> data_lock_;
  std::optional<AttachResult> attached_;

  base::Lock lock_;
  base::ConditionVariable idle_{&lock_};
  int busy_ GUARDED_BY(lock_) = 0;
  scoped_refptr<LaunchAttach> launch_ GUARDED_BY(lock_);
  // The image's device, as this Pane attached and opened it, or as a lock
  // that could not detach it left it.
  std::optional<std::string> device_ GUARDED_BY(lock_);
  // The launch's, until a lock: detached only while the image is still on it,
  // since a device detached gives its number to the next disk attached
  // (0.19.1).
  std::optional<std::string> remembered_ GUARDED_BY(lock_);
  base::Process watchdog_ GUARDED_BY(lock_);
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_VOLUME_PLACE_H_
