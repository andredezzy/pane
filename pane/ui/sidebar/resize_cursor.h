// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_RESIZE_CURSOR_H_
#define PANE_UI_SIDEBAR_RESIZE_CURSOR_H_

#include "ui/base/cursor/cursor.h"

namespace pane {

// Where the sidebar's width stands against its range.
enum class ResizeLimit { kNone, kNarrowest, kWidest };

// The cursor over the sidebar's edge: the column cursor, which at either
// limit points the one way left (macOS 15's columnResize(directions:); before
// it, the left-right resize cursor, either way).
ui::Cursor ResizeCursor(ResizeLimit limit);

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_RESIZE_CURSOR_H_
