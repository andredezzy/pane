// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/persisted_file.h"

#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/types/expected_macros.h"

namespace pane::model {

PersistedFile::PersistedFile(const base::FilePath& directory,
                             std::string_view name)
    : path_(directory.AppendASCII(std::string(name) + ".json")) {}

base::expected<std::optional<base::Value>, FileError> PersistedFile::Load()
    const {
  ASSIGN_OR_RETURN(std::optional<base::Value> envelope, ReadJsonFile(path_));
  if (!envelope) {
    return std::nullopt;
  }
  base::Value* state =
      envelope->is_dict() ? envelope->GetDict().Find("state") : nullptr;
  if (!state) {
    return base::unexpected(FileError::kMalformed);
  }
  return std::move(*state);
}

bool PersistedFile::Save(base::Value state) const {
  const std::optional<std::string> text = Encode(std::move(state));
  return text && WriteTextFile(path_, *text);
}

// static
std::optional<std::string> PersistedFile::Encode(base::Value state) {
  return base::WriteJson(base::Value(
      base::DictValue().Set("state", std::move(state)).Set("version", 0)));
}

void KeepDamaged(const base::FilePath& path) {
  const base::FilePath damaged = path.AddExtensionASCII("damaged");
  if (!base::PathExists(damaged)) {
    base::CopyFile(path, damaged);
  }
}

base::expected<std::optional<base::Value>, FileError> ReadJsonFile(
    const base::FilePath& path) {
  if (!base::PathExists(path)) {
    return std::nullopt;
  }
  std::string text;
  if (!base::ReadFileToString(path, &text)) {
    return base::unexpected(FileError::kUnreadable);
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  if (!value) {
    return base::unexpected(FileError::kMalformed);
  }
  return std::move(value);
}

bool WriteJsonFile(const base::FilePath& path, const base::Value& value) {
  const std::optional<std::string> text = base::WriteJson(value);
  return text && WriteTextFile(path, *text);
}

bool WriteTextFile(const base::FilePath& path, std::string_view text) {
  return base::CreateDirectory(path.DirName()) &&
         base::ImportantFileWriter::WriteFileAtomically(path, text);
}

}  // namespace pane::model
