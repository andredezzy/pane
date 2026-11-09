// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PERSISTED_FILE_H_
#define PANE_MODEL_PERSISTED_FILE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace pane::model {

// Why a file under the data root would not read.
enum class FileError {
  // The disk would not give it.
  kUnreadable,
  // Not JSON, or not in the form its kind writes.
  kMalformed,
  // A sealed file under another key, or changed or moved since it was sealed.
  kRefused,
};

// One JSON file under the data root, in the envelope the Electron app's store
// wrote ({"state": …, "version": 0}), so its files read back unchanged.
class PersistedFile {
 public:
  PersistedFile(const base::FilePath& directory, std::string_view name);

  const base::FilePath& path() const { return path_; }

  // The envelope's state; nullopt when the file does not exist yet.
  base::expected<std::optional<base::Value>, FileError> Load() const;

  // Atomically, with sorted keys.
  bool Save(base::Value state) const;

  // The file's text for `state`, as Save writes it: its envelope, as JSON,
  // keys sorted. nullopt for a value JSON can't hold.
  static std::optional<std::string> Encode(base::Value state);

 private:
  base::FilePath path_;
};

// A file that will not read, kept as <name>.json.damaged before a save writes
// over it. The first one kept stays.
void KeepDamaged(const base::FilePath& path);

// Reads `path` as JSON: nullopt when it does not exist.
base::expected<std::optional<base::Value>, FileError> ReadJsonFile(
    const base::FilePath& path);

// Writes `value` as JSON to `path` atomically, making its directory.
bool WriteJsonFile(const base::FilePath& path, const base::Value& value);

// Writes `text` to `path` atomically, making its directory.
bool WriteTextFile(const base::FilePath& path, std::string_view text);

}  // namespace pane::model

#endif  // PANE_MODEL_PERSISTED_FILE_H_
