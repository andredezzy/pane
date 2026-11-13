// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_DATA_PLACE_H_
#define PANE_MODEL_DATA_PLACE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/atomic_flag.h"
#include "base/types/expected.h"
#include "pane/model/data_key.h"

namespace pane::model {

// When the attach that opened the data started and ended, in milliseconds
// since 1970: for the security scenario and benchmark-unlock.
struct AttachTimes {
  friend bool operator==(const AttachTimes&, const AttachTimes&) = default;

  double started = 0;
  double ended = 0;
};

// The engine's conversion of a profile's files (ADR 0121), its restore from
// the safety copy, and its repair after 0.18.0's migration, through
// //pane/encryption's profile_migration with the profile's key registered:
// the Mac's earlier Panes' (ADR 0122), which Windows never had. Each runs on
// the profile's sequence and returns once it is done, so it is safe to run
// off the window's sequence. `folder` is the profile's folder in the volume
// (DataVolume::Folder); `progress` takes the share converted.
struct FileMigrator {
  FileMigrator();
  FileMigrator(const FileMigrator&);
  FileMigrator& operator=(const FileMigrator&);
  ~FileMigrator();

  base::RepeatingCallback<bool(const std::string& profile_id,
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

// What a profile's sequence does for it before its context opens, and behind
// the window for the profiles not shown (DataVault::Prepare), as the vault
// posted it.
struct ProfileWork {
  ProfileWork();
  ProfileWork(ProfileWork&&);
  ProfileWork& operator=(ProfileWork&&);
  ~ProfileWork();

  // The profile's key, until a lock takes it: what needs it then stops.
  std::optional<Key> KeyWhileUnlocked() const;

  std::string profile_id;
  // A window waits for it: its folders are made, and its safety copy counts
  // the open.
  bool opening = false;
  bool saves_history = true;
  // nullopt once locked, as the vault's own is.
  std::optional<Key> engine_key;
  FileMigrator migrator;
  // Set by the lock that ends the open this was posted in.
  scoped_refptr<base::RefCountedData<base::AtomicFlag>> locked;
  // To the vault: a conversion starts, gets on, and ends.
  base::RepeatingClosure started;
  base::RepeatingCallback<void(double)> progress;
  base::RepeatingCallback<void(bool)> ended;
};

// Where the profiles' data lives, as the vault opens and closes it (ADR 0113,
// U5): on the Mac an encrypted volume, attached once the key opens and
// detached at a lock and at quit (mac/volume_place.h); on Windows, which has
// no volume, the engine's own folder, a profile's at Engine\<id>
// (folder_place.h). The vault keeps the order of everything it posts; a place
// does each step where the vault says.
class DataPlace : public base::RefCountedThreadSafe<DataPlace> {
 public:
  // What a lock's close came to.
  enum class Closed {
    // Nothing was open.
    kNothing,
    // A profile would not close: the data stays open for the exit.
    kLeft,
    // Closed: no process has a profile's file open, and each may forget its
    // keys (ADR 0121).
    kClosed,
    // It would not close within the lock's wait: Pane quits (ADR 0116).
    kQuit,
  };

  using AttachResult = base::expected<AttachTimes, std::string>;

  DataPlace(const DataPlace&) = delete;
  DataPlace& operator=(const DataPlace&) = delete;

  // On the data's sequence, in the order the vault posts them.

  // Opens the data with the key, for the open's Finish to report.
  virtual void Attach(const Key& key) = 0;

  // Once open: moves an earlier Pane's plain folders in when `moving` names
  // them, `moved` told how far, and purges the profiles deleted while it was
  // closed (deletions.json); `reply` gets the attach's outcome.
  virtual void Finish(std::optional<std::vector<std::string>> moving,
                      base::RepeatingCallback<void(double)> moved,
                      base::OnceCallback<void(AttachResult)> reply) = 0;

  // A lock's close, once every profile's work has ended and the engine has
  // closed every profile it could (`every_closed`).
  virtual void CloseAtLock(bool every_closed,
                           base::OnceCallback<void(Closed)> reply) = 0;

  // At exit, on the main thread, after the engine and the thread pool have
  // stopped: whatever is still open closes.
  virtual void CloseAtExit() = 0;

  // On a sequence that may block: whether an earlier Pane's plain folder
  // waits in any of the profiles' places.
  virtual bool HasEarlierData(const std::vector<std::string>& ids) const = 0;

  // On the profile's sequence.

  // Brings the profile up to date before its context opens (`work.opening`),
  // or behind the window: whether it may open.
  virtual bool BringUpToDate(const ProfileWork& work) = 0;
  // The profile has been up 5 s: its safety copy counts the open.
  virtual void ProfileCameUp(const std::string& profile_id) = 0;
  // A deleted profile's data: whether it is all gone.
  virtual bool Remove(const std::string& profile_id) = 0;

 protected:
  friend class base::RefCountedThreadSafe<DataPlace>;

  DataPlace() = default;
  virtual ~DataPlace() = default;
};

}  // namespace pane::model

#endif  // PANE_MODEL_DATA_PLACE_H_
