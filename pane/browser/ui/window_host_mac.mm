// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/window_host.h"

#import <AppKit/AppKit.h>

#include "ui/views/widget/widget.h"

namespace pane {

void MakeKeyAndOrderFront(views::Widget& widget) {
  [widget.GetNativeWindow().GetNativeNSWindow() makeKeyAndOrderFront:nil];
}

}  // namespace pane
