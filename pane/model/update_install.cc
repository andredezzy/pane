// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/update_install.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"

namespace pane::model {

namespace {

constexpr std::string_view kMountPrefix = "pane-update-";

bool IsUuid(std::string_view text) {
  return base::Uuid::ParseCaseInsensitive(text).is_valid();
}

}  // namespace

std::optional<std::string> UpdateInstallTarget(std::string_view bundle_path) {
  if (!bundle_path.ends_with(".app") ||
      bundle_path.find("/AppTranslocation/") != std::string_view::npos ||
      bundle_path.starts_with("/Volumes/")) {
    return std::nullopt;
  }
  return std::string(bundle_path);
}

std::string StagedUpdateName(std::string_view version) {
  return base::StrCat({".Pane-", version, ".app"});
}

bool IsUpdateLeftover(std::string_view name) {
  return name.starts_with(".Pane-") && name.ends_with(".app");
}

std::string UpdateMountName(int32_t pid) {
  // In capitals, as every Pane before this one named its mounts.
  return base::StrCat(
      {kMountPrefix, base::NumberToString(pid), "-",
       base::ToUpperASCII(base::Uuid::GenerateRandomV4().AsLowercaseString())});
}

bool IsLeftoverUpdateMount(std::string_view name,
                           base::FunctionRef<bool(int32_t pid)> is_running) {
  if (!name.starts_with(kMountPrefix)) {
    return false;
  }
  const std::string_view rest = name.substr(kMountPrefix.size());
  if (IsUuid(rest)) {
    return true;
  }

  const size_t separator = rest.find('-');
  int32_t pid = 0;
  if (separator == std::string_view::npos ||
      !base::StringToInt(rest.substr(0, separator), &pid) ||
      !IsUuid(rest.substr(separator + 1))) {
    return false;
  }
  return !is_running(pid);
}

}  // namespace pane::model
