// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_HOST_PLATFORM_H_
#define PANE_MODEL_HOST_PLATFORM_H_

#include <optional>
#include <string>
#include <string_view>

#include "pane/model/profile.h"

namespace pane::model {

// The platform Pane runs on (commit db6b675): a new profile's fingerprint
// claims it unless the user picks another, one on it claims the computer's own
// GPU (GeneratedFingerprint), and one on another hides the computer's own
// voices (EngineFingerprint). The Mac's on the Mac and Windows' on Windows, so
// what a Mac profile saved reads back the same.
Platform HostPlatform();

// What the profile sheet and Settings call the computer Pane runs on: "Sites
// see this Mac.", and "PC" on Windows (Paper 11.7).
std::string_view HostDeviceName();

// What Pane's data rests on here, Settings' last row in Security on Windows
// (U5, Paper 11.19): there no encrypted volume hides the files, and Windows
// gives the key's secret to any app of the user's, so it says what a PIN
// adds, the PC's security chip where `chip_guards_pin`, and what shows
// anyway. None on the Mac, whose volume and Keychain keep the promise (ADR
// 0113).
std::optional<std::string> HostDataPromise(bool chip_guards_pin);

}  // namespace pane::model

#endif  // PANE_MODEL_HOST_PLATFORM_H_
