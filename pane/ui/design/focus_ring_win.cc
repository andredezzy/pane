// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/focus_ring.h"

#include "pane/ui/design/palette.h"

namespace pane {

// Windows' weight (Paper 11.17 B, the user's call): 2 points in the text's
// color, 17.8:1 on the light panel and 16.3:1 on the dark one, where the
// Mac's 1-point grey falls under WCAG's 3:1 for a focus ring; on a strong
// fill, whose color it is, 1 point off the edge, the card between (11.18);
// Highlight under a contrast theme.
FocusRingStyle ControlFocusRingStyle() {
  return {2, kPaneColorKeyboardRing, 1};
}

}  // namespace pane
