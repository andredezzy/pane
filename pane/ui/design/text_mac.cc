// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text.h"

#include "ui/gfx/font_list.h"

namespace pane {

// From the platform's UI font, so the Mac's stays the system font (its
// optical sizes and weights), at the size asked for wherever its default size
// differs.
gfx::FontList PlatformSystemFont(int size, gfx::Font::Weight weight) {
  const gfx::FontList base;
  return base.Derive(size - base.GetFontSize(), gfx::Font::NORMAL, weight);
}

// Grayscale, as the Swift's was (ADR 0060).
bool LabelsTakeClearType() {
  return false;
}

}  // namespace pane
