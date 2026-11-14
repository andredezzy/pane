// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#include "pane/browser/ui/window_host.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

// The taskbar's, Alt+Tab's, and the title bar's icon: each window's, as
// Windows shows an app's, which it takes as the window gives it (WM_SETICON,
// through Widget::UpdateWindowIcon), at its own sizes, from the sky's image
// as the Mac's Dock shows it. The executable's own icon, which shows before
// the window does and in the Explorer, is Chromium's until Windows' sizes are
// drawn (W5).
// static
void DockIcon::SetImage(const gfx::ImageSkia& image) {
  WindowHost::SetIcon(image);
}

}  // namespace pane
