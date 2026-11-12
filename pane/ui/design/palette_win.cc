// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#include <windows.h>

#include "skia/ext/skia_utils_win.h"

namespace pane {

// Windows keeps one selection color, its highlight, in either appearance and
// whether the window has the keys or not.
SkColor SystemTextSelection(bool dark, bool key_window) {
  return skia::COLORREFToSkColor(GetSysColor(COLOR_HIGHLIGHT));
}

}  // namespace pane
