// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#include "ui/gfx/image/image_skia.h"

namespace pane {

// The taskbar's icon, the window's (Widget::UpdateWindowIcon, from an .ico),
// is W4's (docs/plans/views-migration.md): the executable's shows until then.
// static
void DockIcon::SetImage(const gfx::ImageSkia& image) {}

}  // namespace pane
