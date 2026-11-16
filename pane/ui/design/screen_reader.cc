// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/screen_reader.h"

#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

// What both systems' halves share.

namespace pane::screen_reader {

bool PressToExpand(views::View& button, const ui::AXActionData& action) {
  if (action.action != ax::mojom::Action::kExpand &&
      action.action != ax::mojom::Action::kCollapse) {
    return false;
  }
  ui::AXNodeData data;
  button.GetViewAccessibility().GetAccessibleNodeData(&data);
  // Only what says it opens something: its popup, or its state.
  if (!data.SupportsExpandCollapse()) {
    return false;
  }
  const bool expanded = data.HasState(ax::mojom::State::kExpanded);
  if ((action.action == ax::mojom::Action::kExpand) != expanded) {
    // Views' own press, a click at its middle.
    ui::AXActionData press;
    press.action = ax::mojom::Action::kDoDefault;
    button.views::View::HandleAccessibleAction(press);
  }
  return true;
}

}  // namespace pane::screen_reader
