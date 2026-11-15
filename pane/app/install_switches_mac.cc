// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/install_switches.h"

#include "base/command_line.h"

namespace pane {

// The Mac installs by the dmg's drag, and updates as ADR 0092 has it: no
// switch of an installer's, and no installer waiting.
std::optional<int> RunInstallSwitch(const base::CommandLine& command_line) {
  return std::nullopt;
}

void FirstWindowPainted() {}

}  // namespace pane
