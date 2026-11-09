// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/local_state.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/types/expected.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

namespace {

constexpr char kDefaultProfile[] = "Default";

}  // namespace

bool StartOnDefaultProfile(const base::FilePath& user_data_directory) {
  const base::FilePath file = user_data_directory.AppendASCII("Local State");
  const base::expected<std::optional<base::Value>, FileError> state =
      ReadJsonFile(file);
  if (!state.has_value()) {
    return false;
  }
  if (!*state) {
    return true;
  }
  const std::optional<base::Value> changed = StartingOnDefaultProfile(**state);
  return !changed || WriteJsonFile(file, *changed);
}

std::optional<base::Value> StartingOnDefaultProfile(const base::Value& state) {
  const base::DictValue* dict = state.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const base::DictValue* profile = dict->FindDict("profile");
  const base::DictValue* profiles =
      profile ? profile->FindDict("info_cache") : nullptr;
  const bool used =
      profiles && std::ranges::any_of(*profiles, [](const auto& entry) {
        const base::DictValue* info = entry.second.GetIfDict();
        return info && info->Find("active_time");
      });
  const std::string* last_used =
      profile ? profile->FindString("last_used") : nullptr;
  const base::ListValue* last_active =
      profile ? profile->FindList("last_active_profiles") : nullptr;
  if (last_used && *last_used == kDefaultProfile &&
      (!last_active || last_active->empty()) && !used &&
      !dict->Find("tab_stats")) {
    return std::nullopt;
  }

  base::DictValue changed = dict->Clone();
  base::DictValue* changed_profile = changed.EnsureDict("profile");
  if (base::Value* info_cache = changed_profile->Find("info_cache")) {
    if (!info_cache->is_dict()) {
      *info_cache = base::Value(base::DictValue());
    }
    for (auto entry : info_cache->GetDict()) {
      if (base::DictValue* info = entry.second.GetIfDict()) {
        info->Remove("active_time");
      }
    }
  }
  changed_profile->Set("last_used", kDefaultProfile);
  changed_profile->Set("last_active_profiles", base::ListValue());
  changed.Remove("tab_stats");
  return base::Value(std::move(changed));
}

}  // namespace pane::model
