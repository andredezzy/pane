// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_DATA_VAULT_H_
#define PANE_MODEL_MAC_DATA_VAULT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/process/process.h"
#include "base/sequence_checker.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "pane/model/data_key.h"
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

// What the data key opens (ADR 0113): Pane's own files, sealed in place, and
// the encrypted volume every profile's engine data lives in. AppLock opens the
// key, with the PIN when there is one, and hands it here. The volume attaches
// off the window's sequence while the window already shows the profiles; a
// profile's context opens only once it is attached and the profile's folders
// and links are in place (Prepare). The first open after an earlier Pane moves
// its plain profile folders in, with the lock's panel showing the move (Paper
// 07.5).
//
// The order the work is posted in gives ADR 0116's and ADR 0121's rules. One
// sequence for the volume (attach, unlock, move in, purge, detach), on which a
// lock's detach is posted only once every profile has closed and every
// profile's work has ended, and what is posted meanwhile, the attach the PIN
// starts, waits behind it. One sequence per profile, which makes its folders,
// restores its safety copy, converts its files, and repairs them, in that
// order, so a restore and a conversion never share a folder; the profile a
// window waits for goes at once, and the others behind it, one at a time.
// Nothing forces a detach while the engine runs: a lock tries for 10 s and
// otherwise has Pane quit, and the one forced detach is at exit, with the
// engine stopped (EngineDidStop). Each lock invalidates what the open it
// closes had under way.
//
// On the window's sequence, but where it says otherwise.
class DataVault {
 public:
  enum class Phase {
    // Nothing of the profiles is read yet.
    kClosed,
    // An earlier Pane's plain folders moving in, or a profile a window waits
    // for converting: share() has how far it is.
    kMoving,
    kOpen,
  };

  // When the attach that opened the data started and ended, in milliseconds
  // since 1970: for the security scenario and benchmark-unlock.
  struct AttachTimes {
    friend bool operator==(const AttachTimes&, const AttachTimes&) = default;

    double started = 0;
    double ended = 0;
  };

  // The app, told what opens and what will not.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The data key opened: the profiles load (they are sealed under its files
    // key), so ProfileIds() has them once this returns, and what each owes
    // runs.
    virtual void DataKeyDidOpen() = 0;

    // The profiles' ids, as the model has them.
    virtual std::vector<std::string> ProfileIds() = 0;

    // The vault is open: with the lock open too, what waited goes on.
    virtual void AccessDidOpen() = 0;

    // The volume would not open once the key had (AppLock::VolumeWontOpen,
    // Paper 07.8): nothing of the profiles can run.
    virtual void VolumeWontOpen() = 0;

    // A lock's detach would not happen within 10 s: a profile keeps its files
    // open, so Pane quits, which closes them, and the volume detaches after
    // (ADR 0116).
    virtual void QuitToCloseVolume() = 0;

    // A lock's detach succeeded: no process has a profile's file open, so each
    // can forget its keys (ADR 0121).
    virtual void VolumeDidDetachAtLock() = 0;
  };

  class Observer : public base::CheckedObserver {
   public:
    // phase() or share() changed.
    virtual void OnVaultPhaseChanged() = 0;
  };

  // The engine's conversion of a profile's files (ADR 0121), its restore from
  // the safety copy, and its repair after 0.18.0's migration, through
  // //pane/encryption's profile_migration with the profile's key registered.
  // Each runs on the profile's sequence and returns once it is done, so it is
  // safe to run off the window's sequence. `folder` is the profile's folder in
  // the volume (DataVolume::Folder); `progress` takes the share converted.
  struct FileMigrator {
    FileMigrator();
    FileMigrator(const FileMigrator&);
    FileMigrator& operator=(const FileMigrator&);
    ~FileMigrator();

    base::RepeatingCallback<bool(
        const std::string& profile_id,
        const Key& engine_key,
        const base::FilePath& folder,
        base::RepeatingCallback<void(double)> progress)>
        migrate;
    base::RepeatingCallback<bool(const std::string& profile_id,
                                 const Key& engine_key,
                                 const base::FilePath& folder)>
        restore;
    base::RepeatingCallback<bool(const std::string& profile_id,
                                 const Key& engine_key,
                                 const base::FilePath& folder)>
        repair;
  };

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

  // Attaches the volume once the key opens: started where the key opened, off
  // the window's sequence, which, busy at launch and as the lock slides away,
  // never holds it back. Runs on any sequence.
  using Attacher = base::OnceCallback<void(const Key& key)>;

  // Where the watchdog is, beside Pane's executable (the app's Contents/MacOS,
  // beside the loader).
  static base::FilePath WatchdogPath();

  // `data_lock`, held from launch, or null when another Pane held it then:
  // this one waits for it at its first attach. `launch`, the image's attach at
  // launch, or null. The thread pool runs by now.
  DataVault(const base::FilePath& data_directory,
            std::optional<base::FilePath> cache_directory,
            scoped_refptr<VolumeTools> tools,
            std::unique_ptr<DataLock> data_lock,
            scoped_refptr<LaunchAttach> launch,
            Delegate* delegate,
            DetachWaits waits = DetachWaits());
  DataVault(const DataVault&) = delete;
  DataVault& operator=(const DataVault&) = delete;
  ~DataVault();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void set_file_migrator(FileMigrator file_migrator);

  Phase phase() const { return phase_; }
  // How far the move or the conversion the panel shows is, 0 to 1, while
  // moving.
  std::optional<double> share() const;
  const DataVolume& volume() const { return volume_; }
  VolumeTools& tools() const { return *tools_; }
  bool is_attached() const { return attached_; }
  // Why the volume would not attach, for the security scenario: a tool's
  // output can name the image's path, so it is no log line.
  const std::optional<std::string>& failure() const { return failure_; }
  const std::optional<AttachTimes>& attached_at() const { return attached_at_; }

  // The data key while unlocked: for Pane's own files, each profile's engine
  // key (ADR 0121), and to seal again with a new PIN.
  const std::optional<Key>& data_key() const { return data_key_; }
  std::optional<Key> FilesKey() const;
  std::optional<Key> EngineKey(std::string_view profile_id) const;

  // An earlier Pane's profiles wait to move in with no PIN to ask first
  // (DataVolume::HasPlainProfiles, read before the window): the panel shows
  // the move from the first frame, as the PIN screen shows from it (ADR
  // 0042).
  void ExpectMove();

  // For the key the PIN, or the launch without one, is about to open.
  Attacher MakeAttacher();

  // The key opened, and the attach it started is posted: profiles an earlier
  // Pane left in the clear move in before any opens, on the first open or one
  // after a quit cut that move short.
  void Open(const Key& key);

  // Runs `body` once the volume is attached, with false when it would not
  // attach, or a lock came first.
  void WhenAttached(base::OnceCallback<void(bool)> body);

  // Brings the profile up to date before its context opens, on its sequence:
  // its folders in the volume and its links, a plain folder an earlier Pane
  // left moved in, its safety copy restored after two opens a kill ended, its
  // files converted (the panel past a second), or repaired. `done` says
  // whether it may open, false once a lock came, and always runs, a lock or
  // not, since the lock waits for the context to settle. Called once
  // WhenAttached said true, and once per open: each call marks an open
  // started for the safety copy, as the Swift's ProfileContexts.with did.
  void Prepare(const std::string& profile_id,
               bool saves_history,
               base::OnceCallback<void(bool)> done);

  // The profile came up: past 5 s, its safety copy counts this open. A lock
  // inside them closed the open, so its timer counts nothing.
  void ProfileDidOpen(const std::string& profile_id);

  // A deleted profile's data: its links, and its folder in the volume.
  // `removed` runs once it is all gone: until then the deletion's record
  // stays, so a quit on the way leaves no folder that nothing names.
  void Remove(const std::string& profile_id, base::OnceClosure removed);

  // Pane locked while open (ADR 0116): the vault starts over at once, closed,
  // for the PIN to open again, and the key leaves memory. The volume detaches
  // behind it, once every profile's work under way has ended and the returned
  // callback says every profile closed in the engine, its stores with it; the
  // attach the PIN starts meanwhile waits for that detach. A profile that would
  // not close keeps its files open, so the image stays attached for the exit
  // to detach once the engine has stopped. The callback must run.
  [[nodiscard]] base::OnceCallback<void(bool every_closed)> Lock();

  // At exit, after the engine has shut down and the thread pool with it, when
  // nothing has the volume's files open: the image detached, mounted or still
  // locked, the 5 s an attach or a lock's detach still under way may take
  // waited for first (the first launch's making of the image, say: a quit in
  // it had left the image attached). On the main thread, where it blocks:
  // it waits on a condition variable, sleeps between tries, and runs hdiutil,
  // which the browser's UI thread disallows (DisallowUnresponsiveTasks), so
  // its caller allows them, as the Swift ran it after the engine returned.
  void EngineDidStop();

 private:
  class Volume;
  class VolumeSequence;
  struct PendingLock;

  // What a lock's detach came to.
  enum class LockDetach {
    // Nothing was attached.
    kNothing,
    // A profile would not close: the image stays attached for the exit.
    kLeft,
    kDetached,
    // It would not detach within 10 s: Pane quits.
    kQuit,
  };

  void OpenWith(std::vector<std::string> ids, bool moving);
  void Finish(std::optional<std::vector<std::string>> moving);
  void Moved(int locks, double share);
  void DidFinish(bool moved, base::expected<AttachTimes, std::string> result);
  void AttachFailed(const std::string& reason);
  void RunAttachWaiters(bool attached);
  void BecomeOpen();
  void SetPhase(Phase phase, double share);

  void PostProfileWork(const std::string& profile_id,
                       bool opening,
                       bool saves_history,
                       base::OnceCallback<void(bool)> done);
  scoped_refptr<base::SequencedTaskRunner> ProfileSequence(
      const std::string& profile_id);
  void ProfileWorkDone(const std::string& profile_id,
                       int locks,
                       base::OnceCallback<void(bool)> done,
                       bool result);
  void EncryptRemainingProfiles(std::vector<std::string> ids);
  void EncryptedOne(std::vector<std::string> ids, bool encrypted);
  void MigrationStarted(const std::string& profile_id, bool waiting, int locks);
  void MigrationProgress(const std::string& profile_id,
                         int locks,
                         double share);
  void MigrationEnded(const std::string& profile_id, int locks, bool migrated);
  void ShowMigrationPastASecond(const std::string& profile_id);
  void ShowMigration(const std::string& profile_id);
  void SafetyCopyCountsOpen(const std::string& profile_id);
  void Removed(base::OnceClosure removed, bool gone);

  void LockFenced(scoped_refptr<PendingLock> pending);
  void LockClosed(scoped_refptr<PendingLock> pending, bool every_closed);
  void DetachWhenSettled(scoped_refptr<PendingLock> pending);
  void DidDetachAtLock(LockDetach outcome);

  const base::FilePath data_directory_;
  const std::optional<base::FilePath> cache_directory_;
  const DataVolume volume_;
  const scoped_refptr<VolumeTools> tools_;
  const raw_ptr<Delegate> delegate_;
  FileMigrator file_migrator_;

  // The volume's work, and its sequence.
  const scoped_refptr<Volume> work_;
  const scoped_refptr<VolumeSequence> volume_sequence_;
  // Each profile's sequence, and the work posted to it that has not answered.
  std::map<std::string, scoped_refptr<base::SequencedTaskRunner>>
      profile_sequences_;
  std::map<std::string, int> pending_;

  Phase phase_ = Phase::kClosed;
  double share_ = 0;
  std::optional<Key> data_key_;
  std::optional<std::string> failure_;
  bool attached_ = false;
  std::optional<AttachTimes> attached_at_;
  std::vector<base::OnceCallback<void(bool)>> attach_waiters_;

  // Locks so far: work an open began reports to the open it began in.
  int locks_ = 0;
  // Set at the lock that ends this open: the profiles' work stops at its next
  // step, as the key it would take is gone.
  scoped_refptr<base::RefCountedData<base::AtomicFlag>> locked_;

  // The profile whose conversion the panel shows: one a window waits for, past
  // a second (Paper 07.5). And how far each conversion under way is.
  std::optional<std::string> showing_;
  std::map<std::string, double> migration_shares_;

  base::ObserverList<Observer> observers_;

  SEQUENCE_CHECKER(sequence_checker_);

  // Invalidated at each lock: what the open it closes began, which reports to
  // no one after.
  base::WeakPtrFactory<DataVault> open_weak_factory_{this};
  base::WeakPtrFactory<DataVault> weak_factory_{this};
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_DATA_VAULT_H_
