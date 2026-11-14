// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PREFERENCE_MACS_H_
#define PANE_MODEL_PREFERENCE_MACS_H_

#include <string>
#include <string_view>

namespace pane::model {

// Chromium's MACs of a profile's tracked preferences, which on Windows it
// keeps a second time outside the profile's folder, in the registry
// (RegistryHashStoreContentsWin, services/preferences/tracked):
// HKCU\<key>\PreferenceMACs\<the profile folder's name>, a value per tracked
// preference, named by it (an installed extension's id among them), holding
// an HMAC of its value. No site and no value, but the profiles' ids and their
// extensions', outside the encrypted data (ADR 0121's Residuals). Pane's key
// is Chromium's own, install_static's Software\Pane; a test copy's is one of
// its own, which never touches the user's (pane/app/data_launch_win.cc).
// Windows' alone: built there. Both block on the registry.

// The MACs of `profile_id` under `key`, forgotten as its folder goes, so a
// deleted profile leaves nothing there. Whether none is left.
bool ForgetPreferenceMacs(const std::wstring& key, std::string_view profile_id);

// Every profile's under `key`, the Default profile's too: the panic wipe's.
// Whether none is left.
bool ForgetEveryPreferenceMacs(const std::wstring& key);

}  // namespace pane::model

#endif  // PANE_MODEL_PREFERENCE_MACS_H_
