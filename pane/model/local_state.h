// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_LOCAL_STATE_H_
#define PANE_MODEL_LOCAL_STATE_H_

#include <optional>

#include "base/files/file_path.h"
#include "base/values.h"

namespace pane::model {

// Chromium's Local State, at the root of the engine's user data dir. At launch
// Chromium opens the profile it names as used last (GetStartupProfile,
// chrome_browser_main.cc), and the ones open last as its fallback: Pane's
// profiles, whose data is in the volume the data key opens later (ADR 0113).
// Before the engine starts, Pane names its Default profile instead, which
// holds nothing of the user's, and no profiles open last: Pane restores its
// own session. Chromium sets them again as profiles open, and writes them as
// it quits, with when each profile was used (active_time) and how many tabs
// were open (tab_stats): those go too, at launch and at quit, since the file
// is outside the volume.

// Points `user_data_directory`'s Local State at Default, if there is one.
// False when it would not read or write.
bool StartOnDefaultProfile(const base::FilePath& user_data_directory);

// The state naming Default as used last, no profiles open last, and no time
// or count of use; nullopt when it says so already, or is not an object.
std::optional<base::Value> StartingOnDefaultProfile(const base::Value& state);

}  // namespace pane::model

#endif  // PANE_MODEL_LOCAL_STATE_H_
