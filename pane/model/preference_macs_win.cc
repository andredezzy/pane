// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/preference_macs.h"

#include <windows.h>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"

namespace pane::model {

namespace {

// The key the MACs live under, as Chromium names it
// (registry_hash_store_contents_win.cc).
constexpr wchar_t kPreferenceMacs[] = L"\\PreferenceMACs";

// `path` under HKCU, and everything below it, gone: Chromium's own deletion
// (DeleteRegistryKey there), in the view Chromium writes.
bool DeleteKey(const std::wstring& path) {
  base::win::RegKey key;
  const LONG opened = key.Open(HKEY_CURRENT_USER, path.c_str(),
                               KEY_SET_VALUE | KEY_WOW64_32KEY);
  if (opened == ERROR_FILE_NOT_FOUND) {
    return true;
  }
  if (opened != ERROR_SUCCESS) {
    LOG(ERROR) << "Could not open a key of preference MACs: " << opened;
    return false;
  }
  const LONG deleted = key.DeleteKey(L"");
  if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND) {
    LOG(ERROR) << "Could not delete a key of preference MACs: " << deleted;
    return false;
  }
  return true;
}

}  // namespace

bool ForgetPreferenceMacs(const std::wstring& key,
                          std::string_view profile_id) {
  // The profile's own key alone: an empty id would name every profile's.
  if (key.empty() || profile_id.empty()) {
    return false;
  }
  return DeleteKey(key + kPreferenceMacs + L"\\" +
                   base::UTF8ToWide(profile_id));
}

bool ForgetEveryPreferenceMacs(const std::wstring& key) {
  if (key.empty()) {
    return false;
  }
  return DeleteKey(key + kPreferenceMacs);
}

}  // namespace pane::model
