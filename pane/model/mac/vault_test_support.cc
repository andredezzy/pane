// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/vault_test_support.h"

#include <fcntl.h>
#include <sys/file.h>

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time_override.h"

namespace pane::model {

base::ScopedFD LockImagesForTesting() {
  base::FilePath temp;
  CHECK(base::GetTempDir(&temp));
  base::ScopedFD lock(HANDLE_EINTR(
      open(temp.Append("pane_unittests-data-volume.lock").value().c_str(),
           O_RDWR | O_CREAT | O_CLOEXEC, 0600)));
  CHECK(lock.is_valid());
  CHECK_EQ(HANDLE_EINTR(flock(lock.get(), LOCK_EX)), 0);
  return lock;
}

bool WaitFor(base::FunctionRef<bool()> condition) {
  for (int round = 0; round < 30'000; ++round) {
    base::RunLoop().RunUntilIdle();
    if (condition()) {
      return true;
    }
    base::PlatformThread::Sleep(base::Milliseconds(1));
  }
  return false;
}

void RunFor(base::TimeDelta duration) {
  const base::TimeTicks end =
      base::subtle::TimeTicksNowIgnoringOverride() + duration;
  while (base::subtle::TimeTicksNowIgnoringOverride() < end) {
    base::RunLoop().RunUntilIdle();
    base::PlatformThread::Sleep(base::Milliseconds(1));
  }
  base::RunLoop().RunUntilIdle();
}

FakeVolumeTools::FakeVolumeTools(const DataVolume& volume) : volume_(volume) {}

FakeVolumeTools::~FakeVolumeTools() {
  base::AutoLock lock(lock_);
  if (watchdog_.IsValid()) {
    watchdog_.Terminate(0, /*wait=*/true);
  }
}

std::vector<std::string> FakeVolumeTools::calls() const {
  base::AutoLock lock(lock_);
  return calls_;
}

int FakeVolumeTools::Count(std::string_view call) const {
  base::AutoLock lock(lock_);
  return static_cast<int>(std::ranges::count(calls_, call));
}

std::optional<std::string> FakeVolumeTools::attached() const {
  base::AutoLock lock(lock_);
  return attached_;
}

void FakeVolumeTools::FailOpens(VolumeError error) {
  base::AutoLock lock(lock_);
  open_failure_ = std::move(error);
}

void FakeVolumeTools::FailDetaches(int count) {
  base::AutoLock lock(lock_);
  detach_failures_ = count;
}

void FakeVolumeTools::HoldOpens(base::WaitableEvent* gate) {
  base::AutoLock lock(lock_);
  open_gate_ = gate;
}

void FakeVolumeTools::SetOnAttach(base::RepeatingClosure on_attach) {
  base::AutoLock lock(lock_);
  on_attach_ = std::move(on_attach);
}

bool FakeVolumeTools::WatchdogEnded(base::TimeDelta timeout) {
  base::Process watchdog;
  {
    base::AutoLock lock(lock_);
    if (!watchdog_.IsValid()) {
      return false;
    }
    watchdog = watchdog_.Duplicate();
  }
  int status = 0;
  if (!watchdog.WaitForExitWithTimeout(timeout, &status)) {
    return false;
  }
  // Reaped: its number may be another process's from now.
  base::AutoLock lock(lock_);
  watchdog_ = base::Process();
  return true;
}

DataVolume::Devices FakeVolumeTools::Attach() {
  if (!attached_) {
    attached_ = base::StrCat({"/dev/disk", base::NumberToString(next_disk_++)});
  }
  return {.image = *attached_, .volume = base::StrCat({*attached_, "s1"})};
}

base::expected<DataVolume::Devices, VolumeError>
FakeVolumeTools::AttachImageOnce() {
  base::AutoLock lock(lock_);
  calls_.push_back("attach-image");
  return Attach();
}

base::expected<DataVolume::Devices, VolumeError> FakeVolumeTools::AttachOrMake(
    std::string_view passphrase) {
  base::RepeatingClosure on_attach;
  {
    base::AutoLock lock(lock_);
    on_attach = on_attach_;
  }
  if (on_attach) {
    on_attach.Run();
  }
  base::AutoLock lock(lock_);
  calls_.push_back("attach-or-make");
  return Attach();
}

base::expected<void, VolumeError> FakeVolumeTools::Open(
    const DataVolume::Devices& devices,
    std::string_view passphrase,
    std::string_view former) {
  base::WaitableEvent* gate = nullptr;
  {
    base::AutoLock lock(lock_);
    calls_.push_back("open");
    gate = open_gate_;
  }
  if (gate) {
    gate->Wait();
  }

  base::AutoLock lock(lock_);
  if (open_failure_) {
    return base::unexpected(*open_failure_);
  }
  if (attached_ != devices.image) {
    return base::unexpected(VolumeError::Tool("mount", 1, "no such disk"));
  }
  // Mounted: the mount point holds what the volume would.
  base::CreateDirectory(volume_.mount_point());
  base::SetPosixFilePermissions(volume_.mount_point(), 0700);
  return base::ok();
}

base::expected<void, VolumeError> FakeVolumeTools::Detach(
    std::string_view device,
    bool force) {
  base::AutoLock lock(lock_);
  calls_.push_back(force ? "force-detach" : "detach");
  if (attached_ != device) {
    return base::unexpected(VolumeError::Tool("hdiutil detach", 1, "no disk"));
  }
  if (!force && detach_failures_ != 0) {
    --detach_failures_;
    return base::unexpected(VolumeError::Tool("hdiutil detach", 16, "busy"));
  }
  attached_.reset();
  return base::ok();
}

bool FakeVolumeTools::Holds(std::string_view device) {
  base::AutoLock lock(lock_);
  return attached_ == device;
}

base::expected<base::Process, VolumeError> FakeVolumeTools::Watch(
    const base::FilePath& helper) {
  base::AutoLock lock(lock_);
  calls_.push_back("watch");
  // A process of the test's own, which the exit ends.
  base::Process process =
      base::LaunchProcess({"/bin/sleep", "60"}, base::LaunchOptions());
  if (!process.IsValid()) {
    return base::unexpected(VolumeError::Tool("sleep", -1, "did not start"));
  }
  watchdog_ = process.Duplicate();
  return process;
}

void FakeVolumeTools::DetachLeftovers() {
  base::AutoLock lock(lock_);
  calls_.push_back("leftovers");
  attached_.reset();
}

FakeFileMigrator::Gate::Gate() = default;
FakeFileMigrator::Gate::~Gate() = default;

void FakeFileMigrator::Gate::Release() {
  released_.Signal();
}

FakeFileMigrator::FakeFileMigrator() = default;
FakeFileMigrator::~FakeFileMigrator() = default;

DataVault::FileMigrator FakeFileMigrator::Get() {
  DataVault::FileMigrator migrator;
  migrator.migrate = base::BindRepeating(
      [](scoped_refptr<FakeFileMigrator> fake, const std::string& id,
         const Key& key, const base::FilePath& folder,
         base::RepeatingCallback<void(double)> progress) {
        return fake->Run("migrate", id, std::move(progress));
      },
      base::WrapRefCounted(this));
  migrator.restore = base::BindRepeating(
      [](scoped_refptr<FakeFileMigrator> fake, const std::string& id,
         const Key& key, const base::FilePath& folder) {
        return fake->Run("restore", id, base::DoNothing());
      },
      base::WrapRefCounted(this));
  migrator.repair = base::BindRepeating(
      [](scoped_refptr<FakeFileMigrator> fake, const std::string& id,
         const Key& key, const base::FilePath& folder) {
        return fake->Run("repair", id, base::DoNothing());
      },
      base::WrapRefCounted(this));
  return migrator;
}

std::vector<std::string> FakeFileMigrator::calls() const {
  base::AutoLock lock(lock_);
  return calls_;
}

int FakeFileMigrator::Count(std::string_view call) const {
  base::AutoLock lock(lock_);
  return static_cast<int>(std::ranges::count(calls_, call));
}

int FakeFileMigrator::most_at_once() const {
  base::AutoLock lock(lock_);
  return most_at_once_;
}

scoped_refptr<FakeFileMigrator::Gate> FakeFileMigrator::Hold(
    std::string_view kind,
    std::string_view profile_id) {
  auto gate = base::MakeRefCounted<Gate>();
  base::AutoLock lock(lock_);
  holds_[base::StrCat({kind, " ", profile_id})] = gate;
  gates_.push_back(gate);
  return gate;
}

void FakeFileMigrator::ReleaseAll() {
  base::AutoLock lock(lock_);
  for (const scoped_refptr<Gate>& gate : gates_) {
    gate->Release();
  }
}

bool FakeFileMigrator::Run(std::string_view kind,
                           const std::string& profile_id,
                           base::RepeatingCallback<void(double)> progress) {
  const std::string call = base::StrCat({kind, " ", profile_id});
  scoped_refptr<Gate> gate;
  {
    base::AutoLock lock(lock_);
    calls_.push_back(call);
    most_at_once_ = std::max(most_at_once_, ++running_[profile_id]);
    if (auto held = holds_.find(call); held != holds_.end()) {
      gate = std::move(held->second);
      holds_.erase(held);
    }
  }
  progress.Run(0.5);
  if (gate) {
    gate->released_.Wait();
  }
  progress.Run(1);
  base::AutoLock lock(lock_);
  --running_[profile_id];
  return true;
}

}  // namespace pane::model
