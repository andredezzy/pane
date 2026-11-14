// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_TAB_ORDER_H_
#define PANE_UI_DESIGN_TAB_ORDER_H_

#include <vector>

namespace views {
class View;
}  // namespace views

namespace pane {

// Tab on Windows as in Chrome, the user's call (2026-11-14, ADR 0019): the
// toolbar's and the sidebar's buttons take no Tab there, as Chrome's
// toolbar's and tabs' don't, and the keys reach them by F6, Alt+Shift+T, and
// the arrows (FocusGroups, pane/ui/window/focus_groups.h); fields and the
// page keep theirs. On the Mac nothing changes: no button takes Tab there
// already, by Views' own rule there.
void KeepButtonsOutOfTab(views::View& view);

// The buttons under `view` the arrows move between, in their order on
// screen: each shown and enabled, and none inside another (a tab's X is its
// row's).
std::vector<views::View*> ButtonsInOrder(views::View& view);

}  // namespace pane

#endif  // PANE_UI_DESIGN_TAB_ORDER_H_
