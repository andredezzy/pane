// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/menu_icon.h"

#include <memory>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/canvas_image_source.h"

namespace pane {

namespace {

constexpr int kSize = 16;
constexpr float kStroke = 1.5f;

class MenuIconSource : public gfx::CanvasImageSource {
 public:
  explicit MenuIconSource(Icon icon)
      : gfx::CanvasImageSource(gfx::Size(kSize, kSize)), icon_(icon) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    PaintIcon(*canvas, icon_, gfx::RectF(0, 0, kSize, kSize), kStroke,
              SK_ColorBLACK);
  }

 private:
  const Icon icon_;
};

}  // namespace

gfx::ImageSkia MenuIconImage(Icon icon) {
  gfx::ImageSkia image(std::make_unique<MenuIconSource>(icon),
                       gfx::Size(kSize, kSize));
  // Both scales drawn now, for a platform image made of them.
  image.GetRepresentation(1.0f);
  image.GetRepresentation(2.0f);
  return image;
}

}  // namespace pane
