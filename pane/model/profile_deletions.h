// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROFILE_DELETIONS_H_
#define PANE_MODEL_PROFILE_DELETIONS_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/function_ref.h"
#include "base/types/expected.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

// Profiles deleted whose engine data may still be on disk, kept in
// deletions.json from the delete until the engine confirms it. A quit, a
// crash, or a context that would not open leaves an id there, and the next
// launch removes that profile's data once the volume is attached, before
// anything opens a profile.
class ProfileDeletions {
 public:
  explicit ProfileDeletions(const base::FilePath& data_directory);
  ~ProfileDeletions();

  // As deletions.json holds them, in the order they were added; none when it
  // will not read.
  std::vector<std::string> Pending() const;

  void Add(const std::string& profile_id);
  void Remove(const std::string& profile_id);

  // Removes each pending profile's data with `remove_profile` (the volume's:
  // its links, a plain folder an earlier Pane left, and its folder in the
  // volume, which is attached by then), and returns what it could not remove,
  // which stays pending, each with its error.
  std::vector<std::string> Purge(
      base::FunctionRef<base::expected<void, std::string>(
          const std::string& profile_id)> remove_profile);

 private:
  void Save(const std::vector<std::string>& ids);

  PersistedFile file_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_PROFILE_DELETIONS_H_
