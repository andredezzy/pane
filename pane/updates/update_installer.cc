// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_installer.h"

#include <utility>

namespace pane {

UpdateInstaller::UpdateInstaller(base::FilePath bundle, std::string identifier)
    : bundle_(std::move(bundle)), identifier_(std::move(identifier)) {}

UpdateInstaller::UpdateInstaller(const UpdateInstaller&) = default;
UpdateInstaller& UpdateInstaller::operator=(const UpdateInstaller&) = default;
UpdateInstaller::~UpdateInstaller() = default;

}  // namespace pane
