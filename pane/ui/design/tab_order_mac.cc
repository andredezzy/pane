// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/tab_order.h"

namespace pane {

// None takes Tab on the Mac already: Views' buttons there are reached only
// with full keyboard access on, as AppKit's.
void KeepButtonsOutOfTab(views::View& view) {}

}  // namespace pane
