// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/menu_icon.h"

#import <AppKit/AppKit.h>

#include <map>

#include "base/no_destructor.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_util_mac.h"

namespace pane {

ui::ImageModel MenuIcon(Icon icon) {
  // Each made once, as every row's menu shows the same.
  static base::NoDestructor<std::map<Icon, ui::ImageModel>> made;
  ui::ImageModel& model = (*made)[icon];
  if (model.IsEmpty()) {
    NSImage* image = gfx::NSImageFromImageSkia(MenuIconImage(icon));
    [image setTemplate:YES];
    model = ui::ImageModel::FromImage(gfx::Image(image));
  }
  return model;
}

}  // namespace pane
