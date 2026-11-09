// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_UPDATE_INSTALL_H_
#define PANE_MODEL_UPDATE_INSTALL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/function_ref.h"

namespace pane::model {

// Where an update installs itself (ADR 0092): over the running app's bundle,
// swapped in whole for a copy staged beside it. Pane replaces only a bundle it
// runs from in place: not a copy macOS translocated to a read-only path, and
// not one on a disk image.

// The bundle to replace, or nullopt when the install is left to the user.
std::optional<std::string> UpdateInstallTarget(std::string_view bundle_path);

// The new version's copy, beside the app on the same volume so one rename
// swaps them, and hidden, so Finder and Launchpad never show it.
std::string StagedUpdateName(std::string_view version);

// What an install leaves behind: a copy staged and never swapped in, or an old
// bundle swapped out.
bool IsUpdateLeftover(std::string_view name);

// Where a dmg mounts, in the temporary folder, while the Pane in it is checked
// and copied: named for the process that mounted it.
std::string UpdateMountName(int32_t pid);

// Whether `name` is such a mount point whose process is gone (`is_running`
// asks about a pid): a Pane that quit while staging, its dmg perhaps still
// attached there. One named before 0.13.1, without a pid, is left either way.
bool IsLeftoverUpdateMount(std::string_view name,
                           base::FunctionRef<bool(int32_t pid)> is_running);

}  // namespace pane::model

#endif  // PANE_MODEL_UPDATE_INSTALL_H_
