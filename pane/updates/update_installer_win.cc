// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_installer.h"

#include <utility>

#include "base/notreached.h"

namespace pane {

// No install of its own yet (docs/plans/views-migration.md, W7): Pane says it
// cannot replace itself, and the user installs the new version by hand.

UpdateInstaller::UpdateInstaller(base::FilePath bundle, std::string identifier)
    : bundle_(std::move(bundle)), identifier_(std::move(identifier)) {}

UpdateInstaller::UpdateInstaller(const UpdateInstaller&) = default;
UpdateInstaller& UpdateInstaller::operator=(const UpdateInstaller&) = default;
UpdateInstaller::~UpdateInstaller() = default;

// static
std::optional<UpdateInstaller> UpdateInstaller::ForRunningApp() {
  return std::nullopt;
}

std::optional<base::FilePath> UpdateInstaller::Stage(
    const base::FilePath& dmg,
    const std::string& version) const {
  NOTREACHED();
}

bool UpdateInstaller::Install(const base::FilePath& staged,
                              const base::FilePath& dmg,
                              const std::optional<Relaunch>& relaunch) const {
  NOTREACHED();
}

void UpdateInstaller::RemoveLeftovers() const {
  NOTREACHED();
}

}  // namespace pane
