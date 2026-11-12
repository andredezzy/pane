// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/resize_cursor.h"

#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"

namespace pane {

// Windows has no one-way column cursor: its left-right one, at every width.
ui::Cursor ResizeCursor(ResizeLimit limit) {
  return ui::Cursor(ui::mojom::CursorType::kColumnResize);
}

}  // namespace pane
