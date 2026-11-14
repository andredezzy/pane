// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_FOLDER_PLACE_H_
#define PANE_MODEL_FOLDER_PLACE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "pane/model/data_place.h"

namespace pane::model {

class DataLock;

// Every profile's engine data in the engine's own folder (EngineFolder), a
// profile's by its id, with no volume around it: Windows' (U5), where one needs
// elevation, and every file a profile writes is encrypted under its Engine
// key instead (ADR 0121). No Pane ran on Windows before, so there is nothing
// to move in, convert, or repair. Opening it takes the data's lock, held from
// launch, or waited for when another Pane held it then; a lock's close leaves
// the engine its keys only where a profile would not close.
class FolderPlace : public DataPlace {
 public:
  // `data_lock`, held from launch, or null when another Pane held it then:
  // this one waits for it at its first open.
  FolderPlace(const base::FilePath& data_directory,
              std::unique_ptr<DataLock> data_lock);

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
  ~FolderPlace() override;

  const base::FilePath data_directory_;

  // On the data's sequence: the data's lock, once this Pane holds it, whether
  // the data is open, and the last open's outcome, which its Finish reports.
  std::unique_ptr<DataLock> data_lock_;
  bool open_ = false;
  std::optional<AttachResult> attached_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_FOLDER_PLACE_H_
