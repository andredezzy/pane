// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/resize_cursor.h"

#import <AppKit/AppKit.h>

#include "base/no_destructor.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/image/image_skia_util_mac.h"

namespace pane {

namespace {

// AppKit's one-way column cursor, as a cursor of Chromium's: its image at 2×
// and its hot spot, which Views makes an NSCursor of again.
ui::Cursor OneWay(NSHorizontalDirections directions) API_AVAILABLE(macos(15)) {
  NSCursor* cursor = [NSCursor columnResizeCursorInDirections:directions];
  const gfx::ImageSkia image = gfx::ImageSkiaFromNSImage(cursor.image);
  const SkBitmap& bitmap = image.GetRepresentation(2.0f).GetBitmap();
  return ui::Cursor::NewCustom(
      bitmap, gfx::Point(cursor.hotSpot.x * 2, cursor.hotSpot.y * 2), 2.0f);
}

}  // namespace

ui::Cursor ResizeCursor(ResizeLimit limit) {
  if (@available(macOS 15, *)) {
    switch (limit) {
      case ResizeLimit::kNarrowest: {
        static base::NoDestructor<ui::Cursor> right(
            OneWay(NSHorizontalDirectionsRight));
        return *right;
      }
      case ResizeLimit::kWidest: {
        static base::NoDestructor<ui::Cursor> left(
            OneWay(NSHorizontalDirectionsLeft));
        return *left;
      }
      case ResizeLimit::kNone:
        break;
    }
  }
  return ui::Cursor(ui::mojom::CursorType::kColumnResize);
}

}  // namespace pane
