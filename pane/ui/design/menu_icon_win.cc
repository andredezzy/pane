// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/menu_icon.h"

#include "base/functional/bind.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace pane {

// Views' menus draw an image as it is: the icon, in the color they give their
// own icons (kColorMenuIcon), as an NSMenu tints a template image.
ui::ImageModel MenuIcon(Icon icon) {
  const gfx::ImageSkia image = MenuIconImage(icon);
  return ui::ImageModel::FromImageGenerator(
      base::BindRepeating(
          [](const gfx::ImageSkia& image, const ui::ColorProvider* colors) {
            return gfx::ImageSkiaOperations::CreateColorMask(
                image,
                colors ? colors->GetColor(ui::kColorMenuIcon) : SK_ColorBLACK);
          },
          image),
      image.size());
}

}  // namespace pane
