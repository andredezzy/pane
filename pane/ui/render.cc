// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/render.h"

#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace pane {

namespace {

// What `view` paints itself, with its children that share its layer.
SkBitmap PaintOwn(views::View* view, float scale) {
  SkBitmap bitmap;
  {
    ::ui::CanvasPainter painter(&bitmap, view->size(), scale,
                                SK_ColorTRANSPARENT,
                                /*is_pixel_canvas=*/true);
    view->Paint(
        views::PaintInfo::CreateRootPaintInfo(painter.context(), view->size()));
  }
  return bitmap;
}

void DrawLayered(SkCanvas* canvas,
                 views::View* view,
                 views::View* root,
                 float scale,
                 float opacity);

// The views under `view` that have a layer of their own, which its paint
// leaves out, in the order they draw; never a page's.
void DrawLayeredDescendants(SkCanvas* canvas,
                            views::View* view,
                            views::View* root,
                            float scale,
                            float opacity) {
  for (views::View* child : view->children()) {
    if (!child->GetVisible() ||
        views::IsViewClass<views::NativeViewHost>(child)) {
      continue;
    }
    if (child->layer()) {
      DrawLayered(canvas, child, root, scale,
                  opacity * child->layer()->opacity());
    } else {
      DrawLayeredDescendants(canvas, child, root, scale, opacity);
    }
  }
}

void DrawLayered(SkCanvas* canvas,
                 views::View* view,
                 views::View* root,
                 float scale,
                 float opacity) {
  gfx::Point origin;
  views::View::ConvertPointToTarget(view, root, &origin);
  if (view->layer()) {
    const gfx::Vector2dF moved = view->layer()->transform().To2dTranslation();
    origin += gfx::Vector2d(moved.x(), moved.y());
  }
  // A layer that clips what it holds (a scroll view's viewport) clips here
  // too, as the compositor does.
  SkAutoCanvasRestore restore(canvas, /*doSave=*/true);
  if (view->layer() && view->layer()->GetMasksToBounds()) {
    const gfx::Rect bounds =
        views::View::ConvertRectToTarget(view, root, view->GetLocalBounds());
    canvas->clipRect(SkRect::MakeXYWH(bounds.x() * scale, bounds.y() * scale,
                                      bounds.width() * scale,
                                      bounds.height() * scale));
  }
  const SkBitmap own = PaintOwn(view, scale);
  SkPaint paint;
  paint.setAlphaf(opacity);
  canvas->drawImage(own.asImage(), origin.x() * scale, origin.y() * scale,
                    SkSamplingOptions(), &paint);
  DrawLayeredDescendants(canvas, view, root, scale, opacity);
}

}  // namespace

std::optional<std::vector<uint8_t>> RenderView(views::View* root, float scale) {
  if (!root || root->size().IsEmpty()) {
    return std::nullopt;
  }
  SkBitmap output;
  output.allocN32Pixels(root->width() * scale, root->height() * scale);
  output.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(output);
  DrawLayered(&canvas, root, root, scale,
              root->layer() ? root->layer()->opacity() : 1);
  return gfx::PNGCodec::EncodeBGRASkBitmap(output,
                                           /*discard_transparency=*/false);
}

}  // namespace pane
