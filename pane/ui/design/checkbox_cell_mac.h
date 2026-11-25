// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_CHECKBOX_CELL_MAC_H_
#define PANE_UI_DESIGN_CHECKBOX_CELL_MAC_H_

class SkBitmap;

namespace pane {

struct SystemCheckboxState;

// AppKit's checkbox, its cell drawn offscreen at `scale` in the appearance
// asked, flipped as its control is (checkbox_mac.cc).
SkBitmap DrawSystemCheckboxCell(const SystemCheckboxState& state, float scale);

}  // namespace pane

#endif  // PANE_UI_DESIGN_CHECKBOX_CELL_MAC_H_
