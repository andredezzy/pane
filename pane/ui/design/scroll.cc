// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/scroll.h"

#include "ui/views/controls/scroll_view.h"

namespace pane {

std::unique_ptr<views::ScrollView> LayeredScrollView() {
  return std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled);
}

}  // namespace pane
