// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/channel_pointer.h"

namespace pane {

// static
base::ScopedClosureRunner ChannelPointer::KeepSystemPointerOut() {
  // None yet: Windows' copies run in the test VM, where no one moves the
  // pointer (docs/building.md, Scenarios).
  return base::ScopedClosureRunner();
}

}  // namespace pane
