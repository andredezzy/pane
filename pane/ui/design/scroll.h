// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_SCROLL_H_
#define PANE_UI_DESIGN_SCROLL_H_

#include <memory>

namespace views {
class ScrollView;
}

namespace pane {

// A scroll view that scrolls as the Mac's does, on every OS: its contents in
// a layer of their own that it moves, laid out at least as wide and as tall
// as what shows of them. Chromium scrolls that way on Apple's systems alone
// (features::kUiCompositorScrollWithLayers, crbug.com/40471184); elsewhere a
// scroll view leaves contents it sizes by their preferred size as wide as
// they ask, so the profile sheet's rows, which take the width they're given,
// were laid out 0 wide on Windows.
std::unique_ptr<views::ScrollView> LayeredScrollView();

}  // namespace pane

#endif  // PANE_UI_DESIGN_SCROLL_H_
