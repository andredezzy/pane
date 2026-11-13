// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_vault.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

#include "base/barrier_closure.h"
#include "base/containers/circular_deque.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/synchronization/lock.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/thread_annotations.h"
#include "pane/model/profile.h"
#include "pane/model/profile_deletions.h"

namespace pane::model {

namespace {

// The panel shows the conversion a window waits for past a second (Paper
// 07.5).
constexpr base::TimeDelta kShowMigrationAfter = base::Seconds(1);
// A profile up this long counts an open for its safety copy (ADR 0121).
constexpr base::TimeDelta kSafeOpenAfter = base::Seconds(5);

// The data's sequence: a quit with a step under way goes on without it,
// and the exit closes what it opened (EngineDidStop).
constexpr base::TaskTraits kDataTraits = {
    base::MayBlock(), base::WithBaseSyncPrimitives(),
    base::TaskPriority::USER_BLOCKING,
    base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN};
// Each profile's, as the engine converted them: a quit mid-way goes on where
// it was, at the next launch. The conversion is the engine's, given in
// (FileMigrator), and may wait on what it starts.
constexpr base::TaskTraits kProfileTraits = {
    base::MayBlock(), base::WithBaseSyncPrimitives(),
    base::TaskPriority::USER_BLOCKING,
    base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN};

}  // namespace

// The data's sequence

// What is posted to the data's sequence, in order, but behind a lock's
// close that is not posted yet: it waits for every profile to close in the
// engine and every profile's work to end, and the attach the PIN starts
// meanwhile waits behind it. Posted from any sequence.
class DataVault::DataSequence
    : public base::RefCountedThreadSafe<DataSequence> {
 public:
  explicit DataSequence(scoped_refptr<base::SequencedTaskRunner> runner)
      : runner_(std::move(runner)) {}
  DataSequence(const DataSequence&) = delete;
  DataSequence& operator=(const DataSequence&) = delete;

  void PostTask(const base::Location& from_here, base::OnceClosure task) {
    base::AutoLock lock(lock_);
    if (holds_.empty()) {
      runner_->PostTask(from_here, std::move(task));
      return;
    }
    holds_.back().waiting.emplace_back(from_here, std::move(task));
  }

  // A lock's detach will come: what is posted from now on waits for it.
  int Hold() {
    base::AutoLock lock(lock_);
    holds_.push_back(Held{.id = ++last_hold_});
    return last_hold_;
  }

  // The held detach, posted once every detach held before it is, then what
  // waited behind it.
  void ReleaseHold(int hold, base::OnceClosure detach) {
    base::AutoLock lock(lock_);
    for (Held& held : holds_) {
      if (held.id == hold) {
        held.detach = std::move(detach);
      }
    }
    while (!holds_.empty() && holds_.front().detach) {
      Held& front = holds_.front();
      runner_->PostTask(FROM_HERE, std::move(front.detach));
      for (auto& [from_here, task] : front.waiting) {
        runner_->PostTask(from_here, std::move(task));
      }
      holds_.pop_front();
    }
  }

 private:
  friend class base::RefCountedThreadSafe<DataSequence>;

  struct Held {
    int id = 0;
    base::OnceClosure detach;
    std::vector<std::pair<base::Location, base::OnceClosure>> waiting;
  };

  ~DataSequence() = default;

  base::Lock lock_;
  const scoped_refptr<base::SequencedTaskRunner> runner_;
  base::circular_deque<Held> holds_ GUARDED_BY(lock_);
  int last_hold_ GUARDED_BY(lock_) = 0;
};

// A lock's close, once every profile's work has ended and every profile has
// closed in the engine.
struct DataVault::PendingLock : base::RefCounted<PendingLock> {
  explicit PendingLock(int hold) : hold(hold) {}

  const int hold;
  bool fenced = false;
  std::optional<bool> every_closed;

 private:
  friend class base::RefCounted<PendingLock>;
  ~PendingLock() = default;
};

// The vault

DataVault::DataVault(const base::FilePath& data_directory,
                     scoped_refptr<DataPlace> place,
                     Delegate* delegate)
    : data_directory_(data_directory),
      delegate_(delegate),
      place_(std::move(place)),
      data_sequence_(base::MakeRefCounted<DataSequence>(
          base::ThreadPool::CreateSequencedTaskRunner(kDataTraits))),
      locked_(base::MakeRefCounted<base::RefCountedData<base::AtomicFlag>>()) {
  CHECK(delegate_);
  CHECK(place_);
}

DataVault::~DataVault() = default;

void DataVault::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void DataVault::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

void DataVault::set_file_migrator(FileMigrator file_migrator) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  file_migrator_ = std::move(file_migrator);
}

std::optional<double> DataVault::share() const {
  if (phase_ != Phase::kMoving) {
    return std::nullopt;
  }
  return share_;
}

std::optional<Key> DataVault::FilesKey() const {
  if (!data_key_) {
    return std::nullopt;
  }
  return ::pane::model::FilesKey(*data_key_);
}

std::optional<Key> DataVault::EngineKey(std::string_view profile_id) const {
  if (!data_key_) {
    return std::nullopt;
  }
  return EngineFilesKey(*data_key_, profile_id);
}

void DataVault::ExpectMove() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetPhase(Phase::kMoving, 0);
}

DataVault::Attacher DataVault::MakeAttacher() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return base::BindOnce(
      [](scoped_refptr<DataSequence> sequence, scoped_refptr<DataPlace> place,
         const Key& key) {
        sequence->PostTask(FROM_HERE,
                           base::BindOnce(&DataPlace::Attach, place, key));
      },
      data_sequence_, place_);
}

void DataVault::Open(const Key& key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  data_key_ = key;
  delegate_->DataKeyDidOpen(base::BindOnce(&DataVault::ProfilesLoaded,
                                           open_weak_factory_.GetWeakPtr()));
}

void DataVault::ProfilesLoaded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // An id that is no folder's name is no profile the engine opens, and never
  // reaches the disk.
  std::vector<std::string> ids = delegate_->ProfileIds();
  std::erase_if(ids,
                [](const std::string& id) { return !IsProfileFolderName(id); });
  // Bound before `ids` moves into the reply: the two arguments of one call
  // are evaluated in no set order.
  base::OnceCallback<bool()> has_plain_folders =
      base::BindOnce(&DataPlace::HasEarlierData, place_, ids);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      std::move(has_plain_folders),
      base::BindOnce(&DataVault::OpenWith, open_weak_factory_.GetWeakPtr(),
                     std::move(ids)));
}

void DataVault::OpenWith(std::vector<std::string> ids, bool moving) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (moving) {
    SetPhase(Phase::kMoving, 0);
  } else {
    BecomeOpen();
  }
  Finish(moving ? std::make_optional(std::move(ids)) : std::nullopt);
}

void DataVault::Finish(std::optional<std::vector<std::string>> moving) {
  const bool moves = moving.has_value();
  scoped_refptr<base::SequencedTaskRunner> here =
      base::SequencedTaskRunner::GetCurrentDefault();
  data_sequence_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &DataPlace::Finish, place_, std::move(moving),
          base::BindPostTask(
              here,
              base::BindRepeating(&DataVault::Moved,
                                  open_weak_factory_.GetWeakPtr(), locks_)),
          base::BindPostTask(
              here, base::BindOnce(&DataVault::DidFinish,
                                   open_weak_factory_.GetWeakPtr(), moves))));
}

void DataVault::Moved(int locks, double share) {
  if (locks == locks_ && phase_ == Phase::kMoving) {
    SetPhase(Phase::kMoving, share);
  }
}

void DataVault::DidFinish(bool moved, DataPlace::AttachResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result.has_value()) {
    AttachFailed(result.error());
    return;
  }

  attached_at_ = *result;
  if (moved) {
    BecomeOpen();
  }
  RunAttachWaiters(true);

  // A profile a window asked for goes first: it asked just above.
  EncryptRemainingProfiles(delegate_->ProfileIds());
}

// No profile's context opens: the window still shows the profiles and their
// tabs.
void DataVault::AttachFailed(const std::string& reason) {
  failure_ = reason;
  delegate_->VolumeWontOpen();
  if (phase_ == Phase::kMoving) {
    BecomeOpen();
  }
  RunAttachWaiters(false);
}

void DataVault::RunAttachWaiters(bool attached) {
  std::vector<base::OnceCallback<void(bool)>> waiters =
      std::exchange(attach_waiters_, {});
  for (base::OnceCallback<void(bool)>& waiter : waiters) {
    std::move(waiter).Run(attached);
  }
}

void DataVault::BecomeOpen() {
  SetPhase(Phase::kOpen, 0);
  delegate_->AccessDidOpen();
}

void DataVault::SetPhase(Phase phase, double share) {
  phase_ = phase;
  share_ = share;
  for (Observer& observer : observers_) {
    observer.OnVaultPhaseChanged();
  }
}

void DataVault::WhenAttached(base::OnceCallback<void(bool)> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attached_at_) {
    std::move(body).Run(true);
  } else if (failure_) {
    std::move(body).Run(false);
  } else {
    attach_waiters_.push_back(std::move(body));
  }
}

void DataVault::Prepare(const std::string& profile_id,
                        bool saves_history,
                        base::OnceCallback<void(bool)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // An id that is no folder's name never reaches the disk.
  if (!IsProfileFolderName(profile_id)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done), false));
    return;
  }
  // Its sequence still has work of its own, which a window now waits for.
  if (pending_.contains(profile_id)) {
    ShowMigrationPastASecond(profile_id);
  }
  PostProfileWork(profile_id, /*opening=*/true, saves_history, std::move(done));
}

void DataVault::PostProfileWork(const std::string& profile_id,
                                bool opening,
                                bool saves_history,
                                base::OnceCallback<void(bool)> done) {
  ++pending_[profile_id];
  scoped_refptr<base::SequencedTaskRunner> here =
      base::SequencedTaskRunner::GetCurrentDefault();

  ProfileWork work;
  work.profile_id = profile_id;
  work.opening = opening;
  work.saves_history = saves_history;
  work.engine_key = EngineKey(profile_id);
  work.migrator = file_migrator_;
  work.locked = locked_;
  work.started = base::BindPostTask(
      here, base::BindRepeating(&DataVault::MigrationStarted,
                                weak_factory_.GetWeakPtr(), profile_id, opening,
                                locks_));
  work.progress = base::BindPostTask(
      here,
      base::BindRepeating(&DataVault::MigrationProgress,
                          weak_factory_.GetWeakPtr(), profile_id, locks_));
  work.ended =
      base::BindPostTask(here, base::BindRepeating(&DataVault::MigrationEnded,
                                                   weak_factory_.GetWeakPtr(),
                                                   profile_id, locks_));

  ProfileSequence(profile_id)
      ->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(&DataPlace::BringUpToDate, place_, std::move(work)),
          base::BindOnce(&DataVault::ProfileWorkDone,
                         weak_factory_.GetWeakPtr(), profile_id, locks_,
                         std::move(done)));
}

scoped_refptr<base::SequencedTaskRunner> DataVault::ProfileSequence(
    const std::string& profile_id) {
  scoped_refptr<base::SequencedTaskRunner>& sequence =
      profile_sequences_[profile_id];
  if (!sequence) {
    sequence = base::ThreadPool::CreateSequencedTaskRunner(kProfileTraits);
  }
  return sequence;
}

void DataVault::ProfileWorkDone(const std::string& profile_id,
                                int locks,
                                base::OnceCallback<void(bool)> done,
                                bool result) {
  if (--pending_[profile_id] == 0) {
    pending_.erase(profile_id);
  }
  // A lock came first: the open it was for is over.
  std::move(done).Run(result && locks == locks_);
}

// Every profile not yet encrypted, one at a time, behind what the window
// shows.
void DataVault::EncryptRemainingProfiles(std::vector<std::string> ids) {
  std::erase_if(ids,
                [](const std::string& id) { return !IsProfileFolderName(id); });
  if (ids.empty()) {
    return;
  }
  const std::string id = ids.front();
  ids.erase(ids.begin());
  PostProfileWork(
      id, /*opening=*/false, /*saves_history=*/true,
      base::BindOnce(&DataVault::EncryptedOne, open_weak_factory_.GetWeakPtr(),
                     std::move(ids)));
}

void DataVault::EncryptedOne(std::vector<std::string> ids, bool encrypted) {
  EncryptRemainingProfiles(std::move(ids));
}

void DataVault::MigrationStarted(const std::string& profile_id,
                                 bool waiting,
                                 int locks) {
  migration_shares_[profile_id] = 0;
  if (waiting && locks == locks_) {
    ShowMigrationPastASecond(profile_id);
  }
}

void DataVault::MigrationProgress(const std::string& profile_id,
                                  int locks,
                                  double share) {
  migration_shares_[profile_id] = share;
  if (showing_ == profile_id) {
    Moved(locks, share);
  }
}

void DataVault::MigrationEnded(const std::string& profile_id,
                               int locks,
                               bool migrated) {
  migration_shares_.erase(profile_id);
  if (showing_ == profile_id) {
    showing_.reset();
    if (locks == locks_) {
      BecomeOpen();
    }
  }
}

// One a window waits for shows, however it started.
void DataVault::ShowMigrationPastASecond(const std::string& profile_id) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DataVault::ShowMigration, open_weak_factory_.GetWeakPtr(),
                     profile_id),
      kShowMigrationAfter);
}

void DataVault::ShowMigration(const std::string& profile_id) {
  const auto share = migration_shares_.find(profile_id);
  if (share == migration_shares_.end() || phase_ != Phase::kOpen) {
    return;
  }
  showing_ = profile_id;
  SetPhase(Phase::kMoving, share->second);
}

void DataVault::ProfileDidOpen(const std::string& profile_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsProfileFolderName(profile_id)) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DataVault::SafetyCopyCountsOpen,
                     open_weak_factory_.GetWeakPtr(), profile_id),
      kSafeOpenAfter);
}

void DataVault::SafetyCopyCountsOpen(const std::string& profile_id) {
  ProfileSequence(profile_id)
      ->PostTask(FROM_HERE,
                 base::BindOnce(&DataPlace::ProfileCameUp, place_, profile_id));
}

void DataVault::RecordDeletion(const std::string& profile_id,
                               base::OnceClosure recorded) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  data_sequence_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](const base::FilePath& directory, const std::string& id,
             scoped_refptr<base::SequencedTaskRunner> reply,
             base::OnceClosure recorded) {
            ProfileDeletions(directory).Add(id);
            reply->PostTask(FROM_HERE, std::move(recorded));
          },
          data_directory_, profile_id,
          base::SequencedTaskRunner::GetCurrentDefault(), std::move(recorded)));
}

void DataVault::Remove(const std::string& profile_id,
                       base::OnceClosure removed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ProfileSequence(profile_id)
      ->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&DataPlace::Remove, place_, profile_id),
          base::BindOnce(&DataVault::Removed, weak_factory_.GetWeakPtr(),
                         profile_id, std::move(removed)));
}

void DataVault::Removed(const std::string& profile_id,
                        base::OnceClosure removed,
                        bool gone) {
  if (!gone) {
    return;
  }
  // The record goes on the sequence the purge writes it on, then the caller
  // hears.
  data_sequence_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](const base::FilePath& directory, const std::string& id,
             scoped_refptr<base::SequencedTaskRunner> reply,
             base::OnceClosure removed) {
            ProfileDeletions(directory).Remove(id);
            reply->PostTask(FROM_HERE, std::move(removed));
          },
          data_directory_, profile_id,
          base::SequencedTaskRunner::GetCurrentDefault(), std::move(removed)));
}

base::OnceCallback<void(bool every_closed)> DataVault::Lock() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++locks_;
  locked_->data.Set();
  locked_ = base::MakeRefCounted<base::RefCountedData<base::AtomicFlag>>();
  open_weak_factory_.InvalidateWeakPtrs();

  data_key_.reset();
  failure_.reset();
  attached_at_.reset();
  SetPhase(Phase::kClosed, 0);

  // A profile waiting for the attach opens no more.
  RunAttachWaiters(false);

  auto pending = base::MakeRefCounted<PendingLock>(data_sequence_->Hold());
  // Every profile's work under way ends first: it has the data's files open.
  base::RepeatingClosure fenced =
      base::BarrierClosure(profile_sequences_.size(),
                           base::BindOnce(&DataVault::LockFenced,
                                          weak_factory_.GetWeakPtr(), pending));
  for (const auto& [id, sequence] : profile_sequences_) {
    sequence->PostTaskAndReply(FROM_HERE, base::DoNothing(), fenced);
  }
  return base::BindOnce(&DataVault::LockClosed, weak_factory_.GetWeakPtr(),
                        pending);
}

void DataVault::LockFenced(scoped_refptr<PendingLock> pending) {
  pending->fenced = true;
  DetachWhenSettled(std::move(pending));
}

void DataVault::LockClosed(scoped_refptr<PendingLock> pending,
                           bool every_closed) {
  pending->every_closed = every_closed;
  DetachWhenSettled(std::move(pending));
}

void DataVault::DetachWhenSettled(scoped_refptr<PendingLock> pending) {
  if (!pending->fenced || !pending->every_closed) {
    return;
  }
  data_sequence_->ReleaseHold(
      pending->hold,
      base::BindOnce(
          &DataPlace::CloseAtLock, place_, *pending->every_closed,
          base::BindPostTask(base::SequencedTaskRunner::GetCurrentDefault(),
                             base::BindOnce(&DataVault::DidDetachAtLock,
                                            weak_factory_.GetWeakPtr()))));
}

void DataVault::DidDetachAtLock(DataPlace::Closed outcome) {
  switch (outcome) {
    case DataPlace::Closed::kClosed:
      delegate_->VolumeDidDetachAtLock();
      break;
    case DataPlace::Closed::kQuit:
      delegate_->QuitToCloseVolume();
      break;
    case DataPlace::Closed::kNothing:
    case DataPlace::Closed::kLeft:
      break;
  }
}

void DataVault::EngineDidStop() {
  place_->CloseAtExit();
}

}  // namespace pane::model
