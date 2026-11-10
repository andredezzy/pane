// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#import <AppKit/AppKit.h>

#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_util_mac.h"

namespace pane {

// static
void DockIcon::SetImage(const gfx::ImageSkia& image) {
  NSApp.applicationIconImage = gfx::NSImageFromImageSkia(image);
}

}  // namespace pane
