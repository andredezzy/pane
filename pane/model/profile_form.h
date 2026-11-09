// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROFILE_FORM_H_
#define PANE_MODEL_PROFILE_FORM_H_

#include "pane/model/profile.h"

namespace pane::model {

// The profile sheet's rules (the Electron app's profile-sheet.tsx and
// default-fingerprints.ts).

// The fingerprint each platform starts from, default-fingerprints.ts's. The
// engine replaces the Chrome version it claims with its own.
Fingerprint FingerprintPreset(Platform platform);

}  // namespace pane::model

#endif  // PANE_MODEL_PROFILE_FORM_H_
