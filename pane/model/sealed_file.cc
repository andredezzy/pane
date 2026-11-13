// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/sealed_file.h"

#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/types/expected_macros.h"

namespace pane::model {

SealedFile::SealedFile(const base::FilePath& directory, std::string_view name)
    : path_(directory.AppendASCII(std::string(name) + ".json")), name_(name) {}

base::expected<std::optional<base::Value>, FileError> SealedFile::Load(
    const Key& key) const {
  ASSIGN_OR_RETURN(std::optional<base::Value> disk, ReadJsonFile(path_));
  if (!disk) {
    return std::nullopt;
  }
  const std::string* box =
      disk->is_dict() ? disk->GetDict().FindString("box") : nullptr;
  if (!box) {
    return base::unexpected(FileError::kMalformed);
  }
  const base::expected<std::vector<uint8_t>, BoxError> json =
      OpenBox(key, *box, base::as_byte_span(name_));
  if (!json.has_value()) {
    return base::unexpected(json.error() == BoxError::kMalformed
                                ? FileError::kMalformed
                                : FileError::kRefused);
  }
  std::optional<base::Value> state =
      base::JSONReader::Read(base::as_string_view(*json), base::JSON_PARSE_RFC);
  if (!state) {
    return base::unexpected(FileError::kMalformed);
  }
  return std::move(state);
}

bool SealedFile::Save(const base::Value& state, const Key& key) const {
  const std::optional<std::string> text = Seal(state, key);
  return text && WriteTextFile(path_, *text);
}

std::optional<std::string> SealedFile::Seal(const base::Value& state,
                                            const Key& key) const {
  const std::optional<std::string> json = base::WriteJson(state);
  if (!json) {
    return std::nullopt;
  }
  return base::WriteJson(base::Value(base::DictValue().Set(
      "box",
      SealBox(key, base::as_byte_span(*json), base::as_byte_span(name_)))));
}

}  // namespace pane::model
