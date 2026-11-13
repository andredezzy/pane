// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_DATA_VAULT_H_
#define PANE_MODEL_DATA_VAULT_H_

#include <map>
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
#include "base/sequence_checker.h"
#include "base/synchronization/atomic_flag.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "pane/model/data_key.h"
#include "pane/model/data_place.h"

namespace pane::model {

// What the data key opens (ADR 0113): Pane's own files, sealed in place, and
// the place every profile's engine data lives in (DataPlace): on the Mac an
// encrypted volume, on Windows the engine's folder (U5). AppLock opens the
// key, with the PIN when there is one, and hands it here. The data opens off
// the window's sequence while the window already shows the profiles; a
// profile's context opens only once it is open and the profile's folders are
// in place (Prepare). The first open after an earlier Pane moves its plain
// profile folders in, with the lock's panel showing the move (Paper 07.5).
//
// The order the work is posted in gives ADR 0116's and ADR 0121's rules. One
// sequence for the data (attach, unlock, move in, purge, detach), on which a
// lock's close is posted only once every profile has closed and every
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

  using AttachTimes = model::AttachTimes;
  using FileMigrator = model::FileMigrator;

  // The app, told what opens and what will not.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The data key opened: the profiles load (they are sealed under its files
    // key), off the window's sequence, and `loaded` runs once ProfileIds()
    // has them, when what each owes runs. A lock before then: neither.
    virtual void DataKeyDidOpen(base::OnceClosure loaded) = 0;

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
    // can forget its keys (ADR 0121). On Windows, with no volume, once every
    // profile has closed.
    virtual void VolumeDidDetachAtLock() = 0;
  };

  class Observer : public base::CheckedObserver {
   public:
    // phase() or share() changed.
    virtual void OnVaultPhaseChanged() = 0;
  };

  // Attaches the volume once the key opens: started where the key opened, off
  // the window's sequence, which, busy at launch and as the lock slides away,
  // never holds it back. Runs on any sequence.
  using Attacher = base::OnceCallback<void(const Key& key)>;

  // `place`, where the profiles' data lives. The thread pool runs by now.
  DataVault(const base::FilePath& data_directory,
            scoped_refptr<DataPlace> place,
            Delegate* delegate);
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
  bool is_attached() const { return attached_at_.has_value(); }
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

  // A deleted profile's record (deletions.json, ADR 0020), written on the
  // volume's sequence, where the open's purge reads and writes the file too:
  // `recorded` runs once it is on the disk, before the engine's data starts
  // going, so a quit, a crash, or a lock on the way leaves its folder named.
  void RecordDeletion(const std::string& profile_id,
                      base::OnceClosure recorded);

  // A deleted profile's data: its links, and its folder in the volume, then
  // its record. `removed` runs once it is all gone: until then the record
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
  // nothing has the data's files open: the place closes what is still open
  // (DataPlace::CloseAtExit), on the Mac the image detached, mounted or still
  // locked, the 5 s an attach or a lock's detach still under way may take
  // waited for first (the first launch's making of the image, say: a quit in
  // it had left the image attached). On the main thread, where it blocks:
  // it waits on a condition variable, sleeps between tries, and runs hdiutil,
  // which the browser's UI thread disallows (DisallowUnresponsiveTasks), so
  // its caller allows them, as the Swift ran it after the engine returned.
  void EngineDidStop();

 private:
  class DataSequence;
  struct PendingLock;

  void ProfilesLoaded();
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
  void Removed(const std::string& profile_id,
               base::OnceClosure removed,
               bool gone);

  void LockFenced(scoped_refptr<PendingLock> pending);
  void LockClosed(scoped_refptr<PendingLock> pending, bool every_closed);
  void DetachWhenSettled(scoped_refptr<PendingLock> pending);
  void DidDetachAtLock(DataPlace::Closed outcome);

  const base::FilePath data_directory_;
  const raw_ptr<Delegate> delegate_;
  FileMigrator file_migrator_;

  // Where the profiles' data lives, and the sequence of its work.
  const scoped_refptr<DataPlace> place_;
  const scoped_refptr<DataSequence> data_sequence_;
  // Each profile's sequence, and the work posted to it that has not answered.
  std::map<std::string, scoped_refptr<base::SequencedTaskRunner>>
      profile_sequences_;
  std::map<std::string, int> pending_;

  Phase phase_ = Phase::kClosed;
  double share_ = 0;
  std::optional<Key> data_key_;
  std::optional<std::string> failure_;
  // Set while the volume is attached.
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

#endif  // PANE_MODEL_DATA_VAULT_H_
