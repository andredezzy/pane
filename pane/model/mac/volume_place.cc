// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/volume_place.h"

#include <signal.h>

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/platform_thread.h"
#include "pane/model/data_lock.h"
#include "pane/model/panic_wipe.h"
#include "pane/model/profile_deletions.h"

namespace pane::model {

namespace {

// How long a Pane started while another held the data waits for it, at its
// first attach.
constexpr base::TimeDelta kDataLockWait = base::Seconds(30);
// How long the exit waits for an attach or a lock's detach still under way:
// the image's making takes 1.5 s.
constexpr base::TimeDelta kExitWait = base::Seconds(5);
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

void EraseMovedCopies(const base::FilePath& data_directory,
                      const std::optional<base::FilePath>& cache_directory) {
  const std::vector<WipeFailure> failures =
      EraseStagedWipes(data_directory, cache_directory);
  if (!failures.empty()) {
    LOG(ERROR) << "Could not erase " << failures.size() << " plain copies";
  }
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

// The place

// A step that changes what is attached, which the exit waits for.
class VolumePlace::Busy {
 public:
  explicit Busy(VolumePlace& place) : place_(place) {
    base::AutoLock lock(place_->lock_);
    ++place_->busy_;
  }
  Busy(const Busy&) = delete;
  Busy& operator=(const Busy&) = delete;
  ~Busy() {
    base::AutoLock lock(place_->lock_);
    if (--place_->busy_ == 0) {
      place_->idle_.Broadcast();
    }
  }

 private:
  const raw_ref<VolumePlace> place_;
};

VolumePlace::DetachWaits::DetachWaits() {
  for (int milliseconds : {0, 50, 100, 200, 400, 800}) {
    at_lock.push_back(base::Milliseconds(milliseconds));
    at_exit.push_back(base::Milliseconds(milliseconds));
  }
  // 10 s at most at a lock: a profile's stores close a moment after its
  // context does.
  at_lock.insert(at_lock.end(), 17, base::Milliseconds(500));
}

VolumePlace::DetachWaits::DetachWaits(const DetachWaits&) = default;
VolumePlace::DetachWaits& VolumePlace::DetachWaits::operator=(
    const DetachWaits&) = default;
VolumePlace::DetachWaits::~DetachWaits() = default;

// static
base::FilePath VolumePlace::WatchdogPath() {
  base::FilePath executable;
  if (!base::PathService::Get(base::FILE_EXE, &executable)) {
    return base::FilePath();
  }
  return executable.DirName().Append("pane-volume-watch");
}

VolumePlace::VolumePlace(const base::FilePath& data_directory,
                         std::optional<base::FilePath> cache_directory,
                         scoped_refptr<VolumeTools> tools,
                         std::unique_ptr<DataLock> data_lock,
                         scoped_refptr<LaunchAttach> launch,
                         DetachWaits waits)
    : volume_(data_directory, cache_directory),
      data_directory_(data_directory),
      cache_directory_(std::move(cache_directory)),
      tools_(std::move(tools)),
      watchdog_path_(WatchdogPath()),
      waits_(std::move(waits)),
      data_lock_(std::move(data_lock)),
      launch_(std::move(launch)) {}

VolumePlace::~VolumePlace() = default;

// Unlocks and mounts the volume the image attached at launch, or attaches it
// now, or makes it the first time. Its outcome waits for the open's Finish.
void VolumePlace::Attach(const Key& key) {
  Busy busy(*this);
  attached_ = AttachOnce(key);
}

// Once mounted: moves an earlier Pane's plain folders in when `moving` names
// them, and purges the profiles deleted while it was detached; the plain
// copies are erased after, off the open.
void VolumePlace::Finish(std::optional<std::vector<std::string>> moving,
                         base::RepeatingCallback<void(double)> moved,
                         base::OnceCallback<void(AttachResult)> reply) {
  // The attach this open started, once: posted just before it.
  const std::optional<AttachResult> attached =
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

// A lock's: never forced, with the engine running, which would pull the files
// from under it, the first flush after that stopping the process (ADR 0116).
// A device remembered from the launch is detached only while the image is
// still there.
void VolumePlace::CloseAtLock(bool every_closed,
                              base::OnceCallback<void(Closed)> reply) {
  Closed outcome = Closed::kNothing;
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
      outcome = Closed::kLeft;
      if (every_closed) {
        volume_.SafetyCopiesClosed();
        outcome = TryDetaching(*device, waits_.at_lock) ? Closed::kClosed
                                                        : Closed::kQuit;
      }
      if (outcome != Closed::kClosed) {
        if (outcome == Closed::kQuit) {
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

// With the engine stopped: the image detached by its device, which takes it
// whole, the engine's helpers given a moment, then forced. An attach or a
// lock's detach still under way is waited for first, up to 5 s.
void VolumePlace::CloseAtExit() {
  const base::TimeTicks deadline = base::TimeTicks::Now() + kExitWait;
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

bool VolumePlace::HasEarlierData(const std::vector<std::string>& ids) const {
  return std::ranges::any_of(
      ids, [&](const std::string& id) { return volume_.HasPlainFolder(id); });
}

// What a migration before 0.18.2 left wrong in a profile, put right once
// before it next opens. One that fails opens as it was, and tries again next
// time.
bool VolumePlace::Repair(const ProfileWork& work) const {
  if (volume_.FilesAreRepaired(work.profile_id)) {
    return true;
  }
  const std::optional<Key> key = work.KeyWhileUnlocked();
  if (!key || !work.migrator.repair) {
    return false;
  }
  if (work.migrator.repair.Run(work.profile_id, *key,
                               volume_.Folder(work.profile_id))) {
    volume_.MarkFilesRepaired(work.profile_id);
  } else {
    LOG(ERROR) << "Could not repair a profile's files";
  }
  return true;
}

// The profile's files in their encrypted formats (ADR 0121), or repaired once
// they are.
bool VolumePlace::EncryptFiles(const ProfileWork& work) const {
  // A profile made without a tab has no folder: nothing to encrypt.
  if (!volume_.HasFolder(work.profile_id)) {
    return true;
  }
  if (volume_.FilesAreEncrypted(work.profile_id)) {
    return Repair(work);
  }
  const std::optional<Key> key = work.KeyWhileUnlocked();
  if (!key || !work.migrator.migrate) {
    return false;
  }

  work.started.Run();
  const bool migrated = work.migrator.migrate.Run(
      work.profile_id, *key, volume_.Folder(work.profile_id), work.progress);
  if (migrated) {
    volume_.MarkFilesEncrypted(work.profile_id);
  } else {
    LOG(ERROR) << "Could not encrypt a profile's files";
  }
  work.ended.Run(migrated);
  return migrated;
}

// On the profile's sequence, so its steps never overlap: its folders and
// links, a plain folder an earlier Pane left moved in; its safety copy, after
// two opens in a row a kill ended, restored once, after which it converts
// again; its conversion; its repair.
bool VolumePlace::BringUpToDate(const ProfileWork& work) {
  // Posted before a lock: the profile opens no more, and nothing marks an
  // open it won't make.
  if (work.locked->data.IsSet()) {
    return false;
  }
  if (work.opening) {
    if (base::expected<void, VolumeError> prepared =
            volume_.Prepare(work.profile_id, work.saves_history);
        !prepared.has_value()) {
      LOG(ERROR) << "Could not prepare a profile's folders: "
                 << LogLine(prepared.error());
      return false;
    }

    while (volume_.SafetyCopyWillOpen(work.profile_id) ==
           DataVolume::SafetyCopyStep::kRestore) {
      const std::optional<Key> key = work.KeyWhileUnlocked();
      if (!key || !work.migrator.restore) {
        break;
      }
      if (!work.migrator.restore.Run(work.profile_id, *key,
                                     volume_.Folder(work.profile_id))) {
        LOG(ERROR) << "Could not restore a profile's safety copy";
        return false;
      }
      volume_.SafetyCopyDidRestore(work.profile_id);
    }
  }
  return EncryptFiles(work);
}

void VolumePlace::ProfileCameUp(const std::string& profile_id) {
  volume_.SafetyCopyDidOpen(profile_id);
}

bool VolumePlace::Remove(const std::string& profile_id) {
  const base::expected<void, VolumeError> gone = volume_.Remove(profile_id);
  if (!gone.has_value()) {
    LOG(ERROR) << "Could not remove a deleted profile's data: "
               << LogLine(gone.error());
  }
  return gone.has_value();
}

base::expected<AttachTimes, std::string> VolumePlace::AttachOnce(
    const Key& key) {
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
    LOG(ERROR) << "Could not open the data volume: " << LogLine(opened.error());
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

std::optional<DataVolume::Devices> VolumePlace::TakeLaunch(
    base::TimeDelta wait) {
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

void VolumePlace::Watch() {
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

std::optional<std::string> VolumePlace::DeviceToDetach(
    base::TimeDelta launch_wait,
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

bool VolumePlace::TryDetaching(const std::string& device,
                               const std::vector<base::TimeDelta>& waits) {
  for (base::TimeDelta wait : waits) {
    base::PlatformThread::Sleep(wait);
    if (tools_->Detach(device, /*force=*/false).has_value()) {
      return true;
    }
  }
  return false;
}

}  // namespace pane::model
