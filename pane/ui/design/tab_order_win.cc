// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/tab_order.h"

#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace pane {

// Views' buttons take Tab on Windows; these keep to assistive technologies
// and the keys FocusGroups gives them, as Chrome's toolbar buttons
// (ToolbarButton).
void KeepButtonsOutOfTab(views::View& view) {
  if (views::IsViewClass<views::Button>(&view)) {
    view.SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  }
  for (views::View* child : view.children()) {
    KeepButtonsOutOfTab(*child);
  }
}

}  // namespace pane
