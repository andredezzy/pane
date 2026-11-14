// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/tab_order.h"

#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace pane {

namespace {

void CollectButtons(views::View& view, std::vector<views::View*>& buttons) {
  for (views::View* child : view.children()) {
    if (!child->GetVisible()) {
      continue;
    }
    if (views::IsViewClass<views::Button>(child)) {
      if (child->GetEnabled()) {
        buttons.push_back(child);
      }
      continue;
    }
    CollectButtons(*child, buttons);
  }
}

}  // namespace

std::vector<views::View*> ButtonsInOrder(views::View& view) {
  std::vector<views::View*> buttons;
  CollectButtons(view, buttons);
  return buttons;
}

}  // namespace pane
