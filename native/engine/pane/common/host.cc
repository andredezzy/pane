// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/common/host.h"

#include "base/command_line.h"

namespace pane {

bool IsHosted() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(PANE_HOSTED_SWITCH);
}

}  // namespace pane
