// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/profile_deletions.h"

#include <optional>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"

namespace pane::model {

namespace {

// An id names one folder of Engine, never a path out of it, with either
// separator.
bool NamesOneFolder(std::string_view profile_id) {
  return !profile_id.empty() && profile_id != "." && profile_id != ".." &&
         profile_id.find_first_of("/\\") == std::string_view::npos;
}

}  // namespace

ProfileDeletions::ProfileDeletions(const base::FilePath& data_directory)
    : file_(data_directory, "deletions") {}

ProfileDeletions::~ProfileDeletions() = default;

std::vector<std::string> ProfileDeletions::Pending() const {
  const base::expected<std::optional<base::Value>, FileError> state =
      file_.Load();
  const base::ListValue* list =
      state.has_value() && *state ? (*state)->GetIfList() : nullptr;
  std::vector<std::string> ids;
  if (!list) {
    return ids;
  }
  for (const base::Value& id : *list) {
    if (!id.is_string()) {
      return {};
    }
    ids.push_back(id.GetString());
  }
  return ids;
}

void ProfileDeletions::Add(const std::string& profile_id) {
  std::vector<std::string> ids = Pending();
  std::erase(ids, profile_id);
  ids.push_back(profile_id);
  Save(ids);
}

void ProfileDeletions::Remove(const std::string& profile_id) {
  std::vector<std::string> ids = Pending();
  std::erase(ids, profile_id);
  Save(ids);
}

std::vector<std::string> ProfileDeletions::Purge(
    base::FunctionRef<base::expected<void, std::string>(
        const std::string& profile_id)> remove_profile) {
  std::vector<std::string> failed;
  for (const std::string& profile_id : Pending()) {
    if (!NamesOneFolder(profile_id)) {
      Remove(profile_id);
      continue;
    }
    const base::expected<void, std::string> removed =
        remove_profile(profile_id);
    if (removed.has_value()) {
      Remove(profile_id);
    } else {
      failed.push_back(base::StrCat({profile_id, ": ", removed.error()}));
    }
  }
  return failed;
}

void ProfileDeletions::Save(const std::vector<std::string>& ids) {
  if (ids.empty()) {
    base::DeleteFile(file_.path());
    return;
  }
  base::ListValue list;
  for (const std::string& id : ids) {
    list.Append(id);
  }
  if (!file_.Save(base::Value(std::move(list)))) {
    LOG(ERROR) << "Could not save deletions.json.";
  }
}

}  // namespace pane::model
