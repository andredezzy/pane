// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_HOST_PLATFORM_H_
#define PANE_MODEL_HOST_PLATFORM_H_

#include <string_view>

#include "pane/model/profile.h"

namespace pane::model {

// The platform Pane runs on (docs/plans/views-migration.md, N4): a new
// profile's fingerprint claims it unless the user picks another, one on it
// claims the computer's own GPU (GeneratedFingerprint), and one on another
// hides the computer's own voices (EngineFingerprint). The Mac's on the Mac
// and Windows' on Windows, so what a Mac profile saved reads back the same.
Platform HostPlatform();

// What the profile sheet calls the computer Pane runs on: "Sites see this
// Mac." Windows' words wait for Paper (W5).
std::string_view HostDeviceName();

}  // namespace pane::model

#endif  // PANE_MODEL_HOST_PLATFORM_H_
