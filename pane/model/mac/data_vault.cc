// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/data_vault.h"

#include <signal.h>

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

#include "base/barrier_closure.h"
#include "base/base_paths.h"
#include "base/containers/circular_deque.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ref.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/thread_annotations.h"
#include "base/threading/platform_thread.h"
#include "pane/model/mac/data_lock.h"
#include "pane/model/mac/panic_wipe.h"
#include "pane/model/profile.h"
#include "pane/model/profile_deletions.h"

namespace pane::model {

namespace {

// How long a Pane started while another held the data waits for it, at its
// first attach.
constexpr base::TimeDelta kDataLockWait = base::Seconds(30);
// How long the exit waits for an attach or a lock's detach still under way:
// the image's making takes 1.5 s.
constexpr base::TimeDelta kExitWait = base::Seconds(5);
// The panel shows the conversion a window waits for past a second (Paper
// 07.5).
constexpr base::TimeDelta kShowMigrationAfter = base::Seconds(1);
// A profile up this long counts an open for its safety copy (ADR 0121).
constexpr base::TimeDelta kSafeOpenAfter = base::Seconds(5);

// The volume's sequence: a quit with a step under way goes on without it,
// and the exit detaches what it attached (EngineDidStop).
constexpr base::TaskTraits kVolumeTraits = {
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
// The plain copies a move left, off the open.
constexpr base::TaskTraits kEraseTraits = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN};

double NowInMilliseconds() {
  return base::Time::Now().InMillisecondsFSinceUnixEpoch();
}

// For the log: what failed, never a path, or what a tool said, which can name
// one.
std::string LogLine(const VolumeError& error) {
  switch (error.kind) {
    case VolumeError::Kind::kTool:
      return base::StrCat({"a disk image tool exited with ",
                           base::NumberToString(error.status)});
    case VolumeError::Kind::kNotEncrypted:
      return "the volume is not encrypted";
    case VolumeError::Kind::kFile:
      return "a file or folder would not change";
  }
}

class DataVolumeTools : public VolumeTools {
 public:
  explicit DataVolumeTools(const DataVolume& volume) : volume_(volume) {}

  base::expected<DataVolume::Devices, VolumeError> AttachImageOnce() override {
    return volume_.AttachImageOnce();
  }

  base::expected<DataVolume::Devices, VolumeError> AttachOrMake(
      std::string_view passphrase) override {
    return volume_.AttachOrMake(passphrase);
  }

  base::expected<void, VolumeError> Open(const DataVolume::Devices& devices,
                                         std::string_view passphrase,
                                         std::string_view former) override {
    return volume_.Open(devices, passphrase, former);
  }

  base::expected<void, VolumeError> Detach(std::string_view device,
                                           bool force) override {
    return volume_.Detach(device, force);
  }

  bool Holds(std::string_view device) override { return volume_.Holds(device); }

  base::expected<base::Process, VolumeError> Watch(
      const base::FilePath& helper) override {
    if (!base::PathExists(helper)) {
      return base::unexpected(
          VolumeError::File("no pane-volume-watch beside Pane"));
    }
    return volume_.Watch(helper);
  }

  void DetachLeftovers() override { volume_.DetachLeftovers(); }

 private:
  ~DataVolumeTools() override = default;

  const DataVolume volume_;
};

// Whether an earlier Pane's plain folder waits in any of the profiles' places.
bool HasPlainFolders(const DataVolume& volume,
                     const std::vector<std::string>& ids) {
  return std::ranges::any_of(
      ids, [&](const std::string& id) { return volume.HasPlainFolder(id); });
}

void EraseMovedCopies(const base::FilePath& data_directory,
                      const std::optional<base::FilePath>& cache_directory) {
  const std::vector<WipeFailure> failures =
      EraseStagedWipes(data_directory, cache_directory);
  if (!failures.empty()) {
    LOG(ERROR) << "Could not erase " << failures.size() << " plain copies";
  }
}

// What a profile's sequence does for it (BringProfileUpToDate).
struct ProfileJob {
  explicit ProfileJob(const DataVolume& volume) : volume(volume) {}
  ProfileJob(ProfileJob&&) = default;
  ProfileJob& operator=(ProfileJob&&) = default;
  ~ProfileJob() = default;

  DataVolume volume;
  std::string profile_id;
  // A window waits for it: its folders are made, and its safety copy counts
  // the open.
  bool opening = false;
  bool saves_history = true;
  // nullopt once locked, as the vault's own is.
  std::optional<Key> engine_key;
  DataVault::FileMigrator migrator;
  // Set by the lock that ends the open this was posted in.
  scoped_refptr<base::RefCountedData<base::AtomicFlag>> locked;
  // To the vault: a conversion starts, gets on, and ends.
  base::RepeatingClosure started;
  base::RepeatingCallback<void(double)> progress;
  base::RepeatingCallback<void(bool)> ended;
};

// The profile's key, until a lock takes it: what needs it then stops.
std::optional<Key> KeyWhileUnlocked(const ProfileJob& job) {
  if (job.locked->data.IsSet()) {
    return std::nullopt;
  }
  return job.engine_key;
}

// What a migration before 0.18.2 left wrong in a profile, put right once
// before it next opens. One that fails opens as it was, and tries again next
// time.
bool Repair(const ProfileJob& job) {
  if (job.volume.FilesAreRepaired(job.profile_id)) {
    return true;
  }
  const std::optional<Key> key = KeyWhileUnlocked(job);
  if (!key || !job.migrator.repair) {
    return false;
  }
  if (job.migrator.repair.Run(job.profile_id, *key,
                              job.volume.Folder(job.profile_id))) {
    job.volume.MarkFilesRepaired(job.profile_id);
  } else {
    LOG(ERROR) << "Could not repair a profile's files";
  }
  return true;
}

// The profile's files in their encrypted formats (ADR 0121), or repaired once
// they are.
bool EncryptFiles(const ProfileJob& job) {
  // A profile made without a tab has no folder: nothing to encrypt.
  if (!job.volume.HasFolder(job.profile_id)) {
    return true;
  }
  if (job.volume.FilesAreEncrypted(job.profile_id)) {
    return Repair(job);
  }
  const std::optional<Key> key = KeyWhileUnlocked(job);
  if (!key || !job.migrator.migrate) {
    return false;
  }

  job.started.Run();
  const bool migrated = job.migrator.migrate.Run(
      job.profile_id, *key, job.volume.Folder(job.profile_id), job.progress);
  if (migrated) {
    job.volume.MarkFilesEncrypted(job.profile_id);
  } else {
    LOG(ERROR) << "Could not encrypt a profile's files";
  }
  job.ended.Run(migrated);
  return migrated;
}

// On the profile's sequence, so its steps never overlap: its folders and
// links, a plain folder an earlier Pane left moved in; its safety copy, after
// two opens in a row a kill ended, restored once, after which it converts
// again; its conversion; its repair.
bool BringProfileUpToDate(ProfileJob job) {
  // Posted before a lock: the profile opens no more, and nothing marks an
  // open it won't make.
  if (job.locked->data.IsSet()) {
    return false;
  }
  if (job.opening) {
    if (base::expected<void, VolumeError> prepared =
            job.volume.Prepare(job.profile_id, job.saves_history);
        !prepared.has_value()) {
      LOG(ERROR) << "Could not prepare a profile's folders: "
                 << LogLine(prepared.error());
      return false;
    }

    while (job.volume.SafetyCopyWillOpen(job.profile_id) ==
           DataVolume::SafetyCopyStep::kRestore) {
      const std::optional<Key> key = KeyWhileUnlocked(job);
      if (!key || !job.migrator.restore) {
        break;
      }
      if (!job.migrator.restore.Run(job.profile_id, *key,
                                    job.volume.Folder(job.profile_id))) {
        LOG(ERROR) << "Could not restore a profile's safety copy";
        return false;
      }
      job.volume.SafetyCopyDidRestore(job.profile_id);
    }
  }
  return EncryptFiles(job);
}

}  // namespace

// static
scoped_refptr<VolumeTools> VolumeTools::For(const DataVolume& volume) {
  return base::MakeRefCounted<DataVolumeTools>(volume);
}

// The launch's attach

class LaunchAttach::Thread : public base::PlatformThread::Delegate {
 public:
  Thread(scoped_refptr<LaunchAttach> attach,
         const DataVolume& volume,
         scoped_refptr<VolumeTools> tools,
         const base::FilePath& watchdog)
      : attach_(std::move(attach)),
        volume_(volume),
        tools_(std::move(tools)),
        watchdog_(watchdog) {}

  void ThreadMain() override {
    base::PlatformThread::SetName("PaneImageAttach");
    if (volume_.Exists()) {
      base::expected<DataVolume::Devices, VolumeError> devices =
          tools_->AttachImageOnce();
      if (devices.has_value()) {
        attach_->devices_ = std::move(*devices);
        base::expected<base::Process, VolumeError> watching =
            tools_->Watch(watchdog_);
        if (watching.has_value()) {
          attach_->watchdog_ = std::move(*watching);
        } else {
          LOG(ERROR) << "Could not start the volume's watchdog: a crash "
                        "leaves the volume attached";
        }
      }
    }
    attach_->done_.Signal();
    delete this;
  }

 private:
  const scoped_refptr<LaunchAttach> attach_;
  const DataVolume volume_;
  const scoped_refptr<VolumeTools> tools_;
  const base::FilePath watchdog_;
};

// static
scoped_refptr<LaunchAttach> LaunchAttach::Start(
    const DataVolume& volume,
    scoped_refptr<VolumeTools> tools,
    const base::FilePath& watchdog) {
  scoped_refptr<LaunchAttach> attach = base::WrapRefCounted(new LaunchAttach());
  auto thread =
      std::make_unique<Thread>(attach, volume, std::move(tools), watchdog);
  if (base::PlatformThread::CreateNonJoinableWithType(
          0, thread.get(), base::ThreadType::kDefault)) {
    // The thread deletes it as it ends.
    std::ignore = thread.release();
  } else {
    LOG(ERROR) << "Could not start the image's attach";
    attach->done_.Signal();
  }
  return attach;
}

LaunchAttach::LaunchAttach() = default;
LaunchAttach::~LaunchAttach() = default;

bool LaunchAttach::Wait(base::TimeDelta timeout) {
  return done_.TimedWait(timeout);
}

std::optional<DataVolume::Devices> LaunchAttach::devices() const {
  DCHECK(done_.IsSignaled());
  return devices_;
}

base::Process LaunchAttach::TakeWatchdog() {
  DCHECK(done_.IsSignaled());
  return std::move(watchdog_);
}

// The volume's sequence

// What is posted to the volume's sequence, in order, but behind a lock's
// detach that is not posted yet: it waits for every profile to close in the
// engine and every profile's work to end, and the attach the PIN starts
// meanwhile waits behind it. Posted from any sequence.
class DataVault::VolumeSequence
    : public base::RefCountedThreadSafe<VolumeSequence> {
 public:
  explicit VolumeSequence(scoped_refptr<base::SequencedTaskRunner> runner)
      : runner_(std::move(runner)) {}
  VolumeSequence(const VolumeSequence&) = delete;
  VolumeSequence& operator=(const VolumeSequence&) = delete;

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
  friend class base::RefCountedThreadSafe<VolumeSequence>;

  struct Held {
    int id = 0;
    base::OnceClosure detach;
    std::vector<std::pair<base::Location, base::OnceClosure>> waiting;
  };

  ~VolumeSequence() = default;

  base::Lock lock_;
  const scoped_refptr<base::SequencedTaskRunner> runner_;
  base::circular_deque<Held> holds_ GUARDED_BY(lock_);
  int last_hold_ GUARDED_BY(lock_) = 0;
};

// The volume's work

// What the vault does on the volume's sequence, but the close at exit, after
// the thread pool has stopped. What is attached, the launch's attach, and the
// watchdog are kept under `lock_`, which that close reads while a step that
// outlived the pool may still run (CONTINUE_ON_SHUTDOWN).
class DataVault::Volume : public base::RefCountedThreadSafe<Volume> {
 public:
  Volume(const DataVolume& volume,
         const base::FilePath& data_directory,
         const std::optional<base::FilePath>& cache_directory,
         scoped_refptr<VolumeTools> tools,
         std::unique_ptr<DataLock> data_lock,
         scoped_refptr<LaunchAttach> launch,
         const base::FilePath& watchdog,
         DetachWaits waits)
      : volume_(volume),
        data_directory_(data_directory),
        cache_directory_(cache_directory),
        tools_(std::move(tools)),
        watchdog_path_(watchdog),
        waits_(std::move(waits)),
        data_lock_(std::move(data_lock)),
        launch_(std::move(launch)) {}
  Volume(const Volume&) = delete;
  Volume& operator=(const Volume&) = delete;

  // Unlocks and mounts the volume the image attached at launch, or attaches
  // it now, or makes it the first time. Its outcome waits for the open's
  // Finish.
  void Attach(const Key& key) {
    Busy busy(*this);
    attached_ = AttachOnce(key);
  }

  // Once mounted: moves an earlier Pane's plain folders in when `moving`
  // names them, and purges the profiles deleted while it was detached; the
  // plain copies are erased after, off the open.
  void Finish(std::optional<std::vector<std::string>> moving,
              base::RepeatingCallback<void(double)> moved,
              base::OnceCallback<void(base::expected<AttachTimes, std::string>)>
                  reply) {
    // The attach this open started, once: posted just before it.
    const std::optional<base::expected<AttachTimes, std::string>> attached =
        std::exchange(attached_, std::nullopt);
    if (!attached) {
      std::move(reply).Run(base::unexpected("the volume did not attach"));
      return;
    }
    if (!attached->has_value()) {
      std::move(reply).Run(*attached);
      return;
    }

    if (moving) {
      double reported = 0;
      if (base::expected<void, VolumeError> moved_in =
              volume_.MoveIn(*moving,
                             [&](double share) {
                               if (share - reported < 0.01 && share != 1) {
                                 return;
                               }
                               reported = share;
                               moved.Run(share);
                             });
          !moved_in.has_value()) {
        LOG(ERROR) << "Could not move an earlier Pane's data in: "
                   << LogLine(moved_in.error());
        std::move(reply).Run(base::unexpected(moved_in.error().ToString()));
        return;
      }
    }

    const std::vector<std::string> failed =
        ProfileDeletions(data_directory_)
            .Purge([&](const std::string& profile_id)
                       -> base::expected<void, std::string> {
              base::expected<void, VolumeError> removed =
                  volume_.Remove(profile_id);
              if (!removed.has_value()) {
                return base::unexpected(removed.error().ToString());
              }
              return base::ok();
            });
    if (!failed.empty()) {
      LOG(ERROR) << "Could not remove the data of " << failed.size()
                 << " deleted profiles";
    }

    if (moving) {
      // On its own: a lock waits for what this step does, and the volume
      // would stay mounted behind the PIN until the erase ended.
      base::ThreadPool::PostTask(
          FROM_HERE, kEraseTraits,
          base::BindOnce(&EraseMovedCopies, data_directory_, cache_directory_));
    }
    std::move(reply).Run(*attached);
  }

  // A lock's: never forced, with the engine running, which would pull the
  // files from under it, the first flush after that stopping the process (ADR
  // 0116). A device remembered from the launch is detached only while the
  // image is still there.
  void DetachAtLock(bool every_closed,
                    base::OnceCallback<void(LockDetach)> reply) {
    LockDetach outcome = LockDetach::kNothing;
    {
      Busy busy(*this);
      // An attach the lock overtook reports to no open.
      attached_.reset();
      const std::optional<std::string> device =
          DeviceToDetach(base::TimeDelta::Max(), /*check_known=*/false);
      {
        base::AutoLock lock(lock_);
        device_.reset();
        remembered_.reset();
      }
      if (device) {
        outcome = LockDetach::kLeft;
        if (every_closed) {
          volume_.SafetyCopiesClosed();
          outcome = TryDetaching(*device, waits_.at_lock)
                        ? LockDetach::kDetached
                        : LockDetach::kQuit;
        }
        if (outcome != LockDetach::kDetached) {
          if (outcome == LockDetach::kQuit) {
            LOG(ERROR) << "Could not detach the data volume at a lock: Pane "
                          "quits to close it";
          }
          // Left attached, for the exit to detach once the engine has
          // stopped.
          base::AutoLock lock(lock_);
          device_ = device;
        }
      }
    }
    std::move(reply).Run(outcome);
  }

  // With the engine stopped: the image detached by its device, which takes
  // it whole, the engine's helpers given a moment, then forced. An attach or a
  // lock's detach still under way is waited for first, up to `wait`.
  void CloseAtExit(base::TimeDelta wait) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + wait;
    bool settled = false;
    {
      base::AutoLock lock(lock_);
      while (busy_ > 0 && base::TimeTicks::Now() < deadline) {
        idle_.TimedWait(deadline - base::TimeTicks::Now());
      }
      settled = busy_ == 0;
    }

    // With a step still under way, a device it may have detached since is
    // checked: its number goes to the next disk attached.
    const std::optional<std::string> device = DeviceToDetach(
        std::max(deadline - base::TimeTicks::Now(), base::TimeDelta()),
        /*check_known=*/!settled);
    if (!device) {
      return;
    }

    volume_.SafetyCopiesClosed();
    const base::TimeTicks started = base::TimeTicks::Now();
    if (!TryDetaching(*device, waits_.at_exit)) {
      if (base::expected<void, VolumeError> forced =
              tools_->Detach(*device, /*force=*/true);
          !forced.has_value()) {
        LOG(ERROR) << "Could not detach the data volume: "
                   << LogLine(forced.error());
        return;
      }
    }

    // The image is off, so nothing is left for the watchdog, which would
    // hold the data's lock through its hdiutil: a Pane started in those 250
    // ms would find the lock taken (ADR 0113).
    base::AutoLock lock(lock_);
    if (watchdog_.IsValid()) {
      kill(watchdog_.Pid(), SIGTERM);
    }
    device_.reset();
    remembered_.reset();
    VLOG(1) << "The data volume detached in "
            << (base::TimeTicks::Now() - started).InSecondsF() << " s";
  }

 private:
  friend class base::RefCountedThreadSafe<Volume>;

  // A step that changes what is attached, which the exit waits for.
  class Busy {
   public:
    explicit Busy(Volume& volume) : volume_(volume) {
      base::AutoLock lock(volume_->lock_);
      ++volume_->busy_;
    }
    Busy(const Busy&) = delete;
    Busy& operator=(const Busy&) = delete;
    ~Busy() {
      base::AutoLock lock(volume_->lock_);
      if (--volume_->busy_ == 0) {
        volume_->idle_.Broadcast();
      }
    }

   private:
    const raw_ref<Volume> volume_;
  };

  ~Volume() = default;

  base::expected<AttachTimes, std::string> AttachOnce(const Key& key) {
    const double started = NowInMilliseconds();
    const std::string passphrase = VolumePassphrase(key);
    const std::string former = FormerVolumePassphrase(key);

    // A Pane started while another held the data takes its lock first, once
    // that one has let go: it quit, and detached the image before, or the
    // engine's singleton ended it.
    std::unique_ptr<DataLock> taken;
    if (!data_lock_) {
      taken = DataLock::Wait(data_directory_, kDataLockWait);
      if (!taken) {
        LOG(ERROR) << "Could not attach the data volume: another Pane holds "
                      "this data";
        return base::unexpected("another Pane holds this data");
      }
    }

    std::optional<DataVolume::Devices> devices =
        TakeLaunch(base::TimeDelta::Max());
    if (!devices) {
      base::expected<DataVolume::Devices, VolumeError> attached =
          tools_->AttachOrMake(passphrase);
      if (!attached.has_value()) {
        LOG(ERROR) << "Could not attach the data volume: "
                   << LogLine(attached.error());
        return base::unexpected(attached.error().ToString());
      }
      devices = std::move(*attached);
      Watch();
    }

    if (base::expected<void, VolumeError> opened =
            tools_->Open(*devices, passphrase, former);
        !opened.has_value()) {
      LOG(ERROR) << "Could not open the data volume: "
                 << LogLine(opened.error());
      return base::unexpected(opened.error().ToString());
    }

    const double ended = NowInMilliseconds();
    VLOG(1) << "The data volume opened in " << (ended - started) / 1000 << " s";

    // Kept even when a lock overtook the attach: the image is this Pane's.
    if (taken) {
      data_lock_ = std::move(taken);
    }
    base::AutoLock lock(lock_);
    device_ = devices->image;
    return AttachTimes{.started = started, .ended = ended};
  }

  // The launch's attach, once it has ended, and once: its devices, which the
  // first attach unlocks, and its watchdog. Waits up to `wait` for it; one
  // still under way then stays for whoever waits next.
  std::optional<DataVolume::Devices> TakeLaunch(base::TimeDelta wait) {
    scoped_refptr<LaunchAttach> launch;
    {
      base::AutoLock lock(lock_);
      launch = launch_;
    }
    if (!launch || !launch->Wait(wait)) {
      return std::nullopt;
    }
    base::AutoLock lock(lock_);
    // Taken meanwhile, by the exit or the volume's sequence.
    if (launch_ != launch) {
      return std::nullopt;
    }
    launch_.reset();
    std::optional<DataVolume::Devices> devices = launch->devices();
    if (base::Process watchdog = launch->TakeWatchdog(); watchdog.IsValid()) {
      watchdog_ = std::move(watchdog);
    }
    if (devices) {
      remembered_ = devices->image;
    }
    return devices;
  }

  // One watchdog for this Pane's life, once started: it finds the image by its
  // path, on whichever device it then has, through every lock.
  void Watch() {
    {
      base::AutoLock lock(lock_);
      if (watchdog_.IsValid()) {
        return;
      }
    }
    base::expected<base::Process, VolumeError> watching =
        tools_->Watch(watchdog_path_);
    if (!watching.has_value()) {
      LOG(ERROR) << "Could not start the volume's watchdog: a crash leaves the "
                    "volume attached";
      return;
    }
    base::AutoLock lock(lock_);
    watchdog_ = std::move(*watching);
  }

  // The device this Pane attached, or a lock left; else the launch's, only
  // while the image is still there. `check_known` checks the first too.
  std::optional<std::string> DeviceToDetach(base::TimeDelta launch_wait,
                                            bool check_known) {
    TakeLaunch(launch_wait);
    std::optional<std::string> known;
    std::optional<std::string> remembered;
    {
      base::AutoLock lock(lock_);
      known = device_;
      remembered = remembered_;
    }
    if (known && (!check_known || tools_->Holds(*known))) {
      return known;
    }
    if (remembered && tools_->Holds(*remembered)) {
      return remembered;
    }
    return std::nullopt;
  }

  bool TryDetaching(const std::string& device,
                    const std::vector<base::TimeDelta>& waits) {
    for (base::TimeDelta wait : waits) {
      base::PlatformThread::Sleep(wait);
      if (tools_->Detach(device, /*force=*/false).has_value()) {
        return true;
      }
    }
    return false;
  }

  const DataVolume volume_;
  const base::FilePath data_directory_;
  const std::optional<base::FilePath> cache_directory_;
  const scoped_refptr<VolumeTools> tools_;
  const base::FilePath watchdog_path_;
  const DetachWaits waits_;

  // On the volume's sequence: the data's lock, once this Pane holds it, and
  // the last attach's outcome, which the open's Finish reports.
  std::unique_ptr<DataLock> data_lock_;
  std::optional<base::expected<AttachTimes, std::string>> attached_;

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

// A lock's detach, once every profile's work has ended and every profile has
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

DataVault::FileMigrator::FileMigrator() = default;
DataVault::FileMigrator::FileMigrator(const FileMigrator&) = default;
DataVault::FileMigrator& DataVault::FileMigrator::operator=(
    const FileMigrator&) = default;
DataVault::FileMigrator::~FileMigrator() = default;

DataVault::DetachWaits::DetachWaits() {
  for (int milliseconds : {0, 50, 100, 200, 400, 800}) {
    at_lock.push_back(base::Milliseconds(milliseconds));
    at_exit.push_back(base::Milliseconds(milliseconds));
  }
  // 10 s at most at a lock: a profile's stores close a moment after its
  // context does.
  at_lock.insert(at_lock.end(), 17, base::Milliseconds(500));
}

DataVault::DetachWaits::DetachWaits(const DetachWaits&) = default;
DataVault::DetachWaits& DataVault::DetachWaits::operator=(const DetachWaits&) =
    default;
DataVault::DetachWaits::~DetachWaits() = default;

// static
base::FilePath DataVault::WatchdogPath() {
  base::FilePath executable;
  if (!base::PathService::Get(base::FILE_EXE, &executable)) {
    return base::FilePath();
  }
  return executable.DirName().Append("pane-volume-watch");
}

DataVault::DataVault(const base::FilePath& data_directory,
                     std::optional<base::FilePath> cache_directory,
                     scoped_refptr<VolumeTools> tools,
                     std::unique_ptr<DataLock> data_lock,
                     scoped_refptr<LaunchAttach> launch,
                     Delegate* delegate,
                     DetachWaits waits)
    : data_directory_(data_directory),
      cache_directory_(std::move(cache_directory)),
      volume_(data_directory_, cache_directory_),
      tools_(std::move(tools)),
      delegate_(delegate),
      work_(base::MakeRefCounted<Volume>(volume_,
                                         data_directory_,
                                         cache_directory_,
                                         tools_,
                                         std::move(data_lock),
                                         std::move(launch),
                                         WatchdogPath(),
                                         std::move(waits))),
      volume_sequence_(base::MakeRefCounted<VolumeSequence>(
          base::ThreadPool::CreateSequencedTaskRunner(kVolumeTraits))),
      locked_(base::MakeRefCounted<base::RefCountedData<base::AtomicFlag>>()) {
  CHECK(delegate_);
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
      [](scoped_refptr<VolumeSequence> sequence, scoped_refptr<Volume> work,
         const Key& key) {
        sequence->PostTask(FROM_HERE,
                           base::BindOnce(&Volume::Attach, work, key));
      },
      volume_sequence_, work_);
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
      base::BindOnce(&HasPlainFolders, volume_, ids);
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
  volume_sequence_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &Volume::Finish, work_, std::move(moving),
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

void DataVault::DidFinish(bool moved,
                          base::expected<AttachTimes, std::string> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result.has_value()) {
    AttachFailed(result.error());
    return;
  }

  attached_ = true;
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
  if (attached_) {
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

  ProfileJob job(volume_);
  job.profile_id = profile_id;
  job.opening = opening;
  job.saves_history = saves_history;
  job.engine_key = EngineKey(profile_id);
  job.migrator = file_migrator_;
  job.locked = locked_;
  job.started = base::BindPostTask(
      here, base::BindRepeating(&DataVault::MigrationStarted,
                                weak_factory_.GetWeakPtr(), profile_id, opening,
                                locks_));
  job.progress = base::BindPostTask(
      here,
      base::BindRepeating(&DataVault::MigrationProgress,
                          weak_factory_.GetWeakPtr(), profile_id, locks_));
  job.ended =
      base::BindPostTask(here, base::BindRepeating(&DataVault::MigrationEnded,
                                                   weak_factory_.GetWeakPtr(),
                                                   profile_id, locks_));

  ProfileSequence(profile_id)
      ->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&BringProfileUpToDate, std::move(job)),
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
                 base::BindOnce(
                     [](const DataVolume& volume, const std::string& id) {
                       volume.SafetyCopyDidOpen(id);
                     },
                     volume_, profile_id));
}

void DataVault::Remove(const std::string& profile_id,
                       base::OnceClosure removed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ProfileSequence(profile_id)
      ->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(
              [](const DataVolume& volume, const std::string& id) {
                const base::expected<void, VolumeError> gone =
                    volume.Remove(id);
                if (!gone.has_value()) {
                  LOG(ERROR) << "Could not remove a deleted profile's data: "
                             << LogLine(gone.error());
                }
                return gone.has_value();
              },
              volume_, profile_id),
          base::BindOnce(&DataVault::Removed, weak_factory_.GetWeakPtr(),
                         std::move(removed)));
}

void DataVault::Removed(base::OnceClosure removed, bool gone) {
  if (gone) {
    std::move(removed).Run();
  }
}

base::OnceCallback<void(bool every_closed)> DataVault::Lock() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++locks_;
  locked_->data.Set();
  locked_ = base::MakeRefCounted<base::RefCountedData<base::AtomicFlag>>();
  open_weak_factory_.InvalidateWeakPtrs();

  data_key_.reset();
  failure_.reset();
  attached_ = false;
  attached_at_.reset();
  SetPhase(Phase::kClosed, 0);

  // A profile waiting for the attach opens no more.
  RunAttachWaiters(false);

  auto pending = base::MakeRefCounted<PendingLock>(volume_sequence_->Hold());
  // Every profile's work under way ends first: it has the volume's files
  // open.
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
  volume_sequence_->ReleaseHold(
      pending->hold,
      base::BindOnce(
          &Volume::DetachAtLock, work_, *pending->every_closed,
          base::BindPostTask(base::SequencedTaskRunner::GetCurrentDefault(),
                             base::BindOnce(&DataVault::DidDetachAtLock,
                                            weak_factory_.GetWeakPtr()))));
}

void DataVault::DidDetachAtLock(LockDetach outcome) {
  switch (outcome) {
    case LockDetach::kDetached:
      delegate_->VolumeDidDetachAtLock();
      break;
    case LockDetach::kQuit:
      delegate_->QuitToCloseVolume();
      break;
    case LockDetach::kNothing:
    case LockDetach::kLeft:
      break;
  }
}

void DataVault::EngineDidStop() {
  work_->CloseAtExit(kExitWait);
}

}  // namespace pane::model
