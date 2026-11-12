// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/system.h"

namespace pane {

// The system's events a scenario stands in for on Windows are W6's
// (docs/plans/views-migration.md): none is posted yet.

void ChangeSystemTimeZone(const std::string& zone) {}

void RequestEnhancedUserInterface() {}

bool PostSystemEvent(const std::string& event) {
  return false;
}

// AppleScript is the Mac's (ADR 0112).
base::expected<std::optional<std::string>, std::string> RunAppleScript(
    const std::string& source) {
  return base::unexpected("Windows runs no AppleScript");
}

}  // namespace pane
