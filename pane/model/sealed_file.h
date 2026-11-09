// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_SEALED_FILE_H_
#define PANE_MODEL_SEALED_FILE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "pane/model/data_key.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

// One file under the data root, sealed under the data key's files key (ADR
// 0113): AES-GCM over the state's JSON, bound to the file's name, so a box
// moved to another file does not open. A plain file never reads as one: only
// the move into the volume reads the plain files these replace, through
// PersistedFile.
class SealedFile {
 public:
  SealedFile(const base::FilePath& directory, std::string_view name);

  const base::FilePath& path() const { return path_; }

  // nullopt when the file does not exist yet. An error for another key, and a
  // box changed or moved since it was sealed.
  base::expected<std::optional<base::Value>, FileError> Load(
      const Key& key) const;

  // Atomically.
  bool Save(const base::Value& state, const Key& key) const;

 private:
  base::FilePath path_;
  std::string name_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_SEALED_FILE_H_
