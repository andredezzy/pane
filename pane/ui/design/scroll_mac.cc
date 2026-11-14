// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/scroll.h"

namespace pane {

// Views' Mac scroller (CocoaScrollBar) is a scroll view's own there.
void TakeSystemScrollBars(views::ScrollView& view) {}

}  // namespace pane
