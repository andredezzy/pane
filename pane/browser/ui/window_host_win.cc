// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/window_host.h"

#include "ui/views/widget/widget.h"

namespace pane {

// Views' own activation: Windows itself keeps the foreground with the app in
// use, refusing it to a window of an app in the background
// (SetForegroundWindow), which flashes on the taskbar instead.
void MakeKeyAndOrderFront(views::Widget& widget) {
  widget.Activate();
}

}  // namespace pane
