// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/interaction_log.h"

namespace pane {

// The interactions' intervals in Windows' own trace (ETW) time frames, whose
// numbers are a real PC's, not the test VM's, which has no GPU (U6,
// docs/plans/views-migration.md): none is traced yet.

uint64_t BeginInteractionInterval(Interaction interaction) {
  return 0;
}

void EndInteractionInterval(Interaction interaction,
                            uint64_t interval,
                            const FrameTimes& frame) {}

}  // namespace pane
