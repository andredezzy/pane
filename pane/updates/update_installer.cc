// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_installer.h"

#include <utility>

namespace pane {

UpdateInstaller::UpdateInstaller(base::FilePath app, std::string identifier)
    : app_(std::move(app)), identifier_(std::move(identifier)) {}

UpdateInstaller::UpdateInstaller(const UpdateInstaller&) = default;
UpdateInstaller& UpdateInstaller::operator=(const UpdateInstaller&) = default;
UpdateInstaller::~UpdateInstaller() = default;

UpdateInstaller::Kept::Kept() = default;
UpdateInstaller::Kept::Kept(const Kept&) = default;
UpdateInstaller::Kept& UpdateInstaller::Kept::operator=(const Kept&) = default;
UpdateInstaller::Kept::~Kept() = default;

}  // namespace pane
