// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/folder_place.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "pane/model/data_lock.h"
#include "pane/model/profile.h"
#include "pane/model/profile_deletions.h"

namespace pane::model {

namespace {

// How long a Pane started while another held the data waits for it, at its
// first open, as the Mac's volume waits (VolumePlace).
constexpr base::TimeDelta kDataLockWait = base::Seconds(30);

// Between tries at a deleted profile's folder, 10 s in all: Chrome closes the
// profile's files a moment after it marks the profile deleted, and NTFS
// deletes no file still open, where the Mac's unlink takes one.
constexpr base::TimeDelta kRemoveWaits[] = {
    base::TimeDelta(),        base::Milliseconds(100), base::Milliseconds(200),
    base::Milliseconds(400),  base::Milliseconds(800), base::Milliseconds(1600),
    base::Milliseconds(3200), base::Milliseconds(3700)};

}  // namespace

FolderPlace::FolderPlace(const base::FilePath& data_directory,
                         std::unique_ptr<DataLock> data_lock)
    : data_directory_(data_directory), data_lock_(std::move(data_lock)) {}

FolderPlace::~FolderPlace() = default;

// Nothing to attach: the data is open once this Pane holds the data's lock,
// which one started while another quit waits for.
void FolderPlace::Attach(const Key& key) {
  const double started = base::Time::Now().InMillisecondsFSinceUnixEpoch();
  if (!data_lock_) {
    data_lock_ = DataLock::Wait(data_directory_, kDataLockWait);
    if (!data_lock_) {
      LOG(ERROR) << "Could not open the data: another Pane holds it";
      attached_ = base::unexpected("another Pane holds this data");
      return;
    }
  }
  open_ = true;
  attached_ =
      AttachTimes{.started = started,
                  .ended = base::Time::Now().InMillisecondsFSinceUnixEpoch()};
}

void FolderPlace::Finish(std::optional<std::vector<std::string>> moving,
                         base::RepeatingCallback<void(double)> moved,
                         base::OnceCallback<void(AttachResult)> reply) {
  // The open this one started, once: posted just before it.
  const std::optional<AttachResult> attached =
      std::exchange(attached_, std::nullopt);
  if (!attached) {
    std::move(reply).Run(base::unexpected("the data did not open"));
    return;
  }
  if (!attached->has_value()) {
    std::move(reply).Run(*attached);
    return;
  }

  // The profiles deleted while it was closed, or that a quit cut short.
  const std::vector<std::string> failed =
      ProfileDeletions(data_directory_)
          .Purge([&](const std::string& profile_id)
                     -> base::expected<void, std::string> {
            if (!Remove(profile_id)) {
              return base::unexpected("its folder would not go");
            }
            return base::ok();
          });
  if (!failed.empty()) {
    LOG(ERROR) << "Could not remove the data of " << failed.size()
               << " deleted profiles";
  }
  std::move(reply).Run(*attached);
}

// Every profile closed in the engine: no process has a profile's file open,
// and each may forget its keys. One that would not close keeps its files
// open, and the engine its keys, until the quit.
void FolderPlace::CloseAtLock(bool every_closed,
                              base::OnceCallback<void(Closed)> reply) {
  attached_.reset();
  if (!open_) {
    std::move(reply).Run(Closed::kNothing);
    return;
  }
  open_ = false;
  std::move(reply).Run(every_closed ? Closed::kClosed : Closed::kLeft);
}

void FolderPlace::CloseAtExit() {}

// No Pane ran on Windows before: nothing of an earlier one's is there.
bool FolderPlace::HasEarlierData(const std::vector<std::string>& ids) const {
  return false;
}

// Nothing to make or convert: the engine makes a profile's folder as it
// opens it (OpenProfile), under the profile's key. A profile that saves no
// history opens without what it cached on disk while it did, as on the Mac
// (DataVolume::Prepare): a download it was running when it was turned off
// kept them until now. What the engine still holds open stays until the next
// open.
bool FolderPlace::BringUpToDate(const ProfileWork& work) {
  if (work.locked->data.IsSet()) {
    return false;
  }
  if (work.opening && !work.saves_history &&
      IsProfileFolderName(work.profile_id)) {
    const base::FilePath folder =
        data_directory_.Append(FILE_PATH_LITERAL("Engine"))
            .AppendASCII(work.profile_id);
    for (const base::FilePath::CharType* item :
         {FILE_PATH_LITERAL("shared_proto_db"), FILE_PATH_LITERAL("Cache"),
          FILE_PATH_LITERAL("Code Cache")}) {
      base::DeletePathRecursively(folder.Append(item));
    }
  }
  return true;
}

void FolderPlace::ProfileCameUp(const std::string& profile_id) {}

// The profile's folder, Engine/<id>, its caches in it, and everything under
// it, tried again while the engine still holds a file of it open. One still
// held past that stays named in deletions.json, for the next open's purge. An
// id that isn't one folder's name is no profile's.
bool FolderPlace::Remove(const std::string& profile_id) {
  if (!IsProfileFolderName(profile_id)) {
    return false;
  }
  const base::FilePath folder =
      data_directory_.Append(FILE_PATH_LITERAL("Engine"))
          .AppendASCII(profile_id);
  for (base::TimeDelta wait : kRemoveWaits) {
    base::PlatformThread::Sleep(wait);
    if (base::DeletePathRecursively(folder)) {
      return true;
    }
  }
  LOG(ERROR) << "Could not remove a deleted profile's data";
  return false;
}

}  // namespace pane::model
