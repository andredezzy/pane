// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/focus_ring.h"

#include "pane/ui/design/palette.h"

namespace pane {

// The Mac's ring (ring-1, Paper 03.4): 1 point in the ring's grey, on the
// edge of a filled button as of any other.
FocusRingStyle ControlFocusRingStyle() {
  return {1, kPaneColorRing, 0};
}

}  // namespace pane
