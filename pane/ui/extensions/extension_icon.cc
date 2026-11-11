// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/extension_icon.h"

#include <cmath>

#include "base/i18n/case_conversion.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/text_utils.h"

namespace pane {

namespace {

// The badge: 9-point text 2 in on each side, 12 tall, rounded 4, its bottom
// 1 above the button's.
constexpr int kBadgeText = 9;
constexpr float kBadgeInset = 2;
constexpr float kBadgeHeight = 12;
constexpr float kBadgeRadius = 4;
constexpr float kBadgeBottom = 1;

// What an unset color is: transparent.
SkColor OrDefault(SkColor color,
                  const ui::ColorProvider& colors,
                  ui::ColorId fallback) {
  return SkColorGetA(color) == SK_AlphaTRANSPARENT ? colors.GetColor(fallback)
                                                   : color;
}

}  // namespace

void PaintExtensionIcon(gfx::Canvas& canvas,
                        const ui::ColorProvider& colors,
                        const gfx::Image& icon,
                        const std::string& name,
                        const gfx::RectF& bounds,
                        int letter_size,
                        float radius) {
  if (!icon.IsEmpty()) {
    // Scaled as SwiftUI's resizable image with high interpolation.
    const gfx::ImageSkia image = icon.AsImageSkia();
    const gfx::Rect place = gfx::ToRoundedRect(bounds);
    canvas.DrawImageInt(image, 0, 0, image.width(), image.height(), place.x(),
                        place.y(), place.width(), place.height(),
                        /*filter=*/true);
    return;
  }
  cc::PaintFlags tile;
  tile.setAntiAlias(true);
  tile.setColor(colors.GetColor(kPaneColorLetterTile));
  canvas.DrawPath(SquirclePath(bounds, radius), tile);
  const std::u16string letter = base::i18n::ToUpper(
      base::UTF8ToUTF16(name).substr(0, name.empty() ? 0 : 1));
  canvas.DrawStringRectWithFlags(
      letter, SystemFont(letter_size, weight::kMedium), SK_ColorWHITE,
      gfx::ToEnclosingRect(bounds), gfx::Canvas::TEXT_ALIGN_CENTER);
}

void PaintBadge(gfx::Canvas& canvas,
                const ui::ColorProvider& colors,
                const std::u16string& text,
                SkColor background,
                SkColor text_color,
                const gfx::RectF& button) {
  if (text.empty()) {
    return;
  }
  const gfx::FontList& font = SystemFont(kBadgeText, weight::kRegular);
  const float width = gfx::GetStringWidthF(text, font) + 2 * kBadgeInset;
  const gfx::RectF badge(button.right() - width,
                         button.bottom() - kBadgeBottom - kBadgeHeight, width,
                         kBadgeHeight);
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(OrDefault(background, colors, kPaneColorBadgeDefault));
  const SkPath shape = SquirclePath(badge, kBadgeRadius);
  canvas.DrawPath(shape, fill);
  // On the edge, as SwiftUI's stroke draws one.
  cc::PaintFlags ring;
  ring.setAntiAlias(true);
  ring.setStyle(cc::PaintFlags::kStroke_Style);
  ring.setStrokeWidth(1);
  ring.setColor(colors.GetColor(kPaneColorBadgeRing));
  canvas.DrawPath(shape, ring);
  gfx::RectF text_bounds = badge;
  text_bounds.Inset(gfx::InsetsF::VH(0, kBadgeInset));
  canvas.DrawStringRectWithFlags(
      text, font, OrDefault(text_color, colors, kPaneColorBadgeDefaultText),
      gfx::ToEnclosingRect(text_bounds), gfx::Canvas::NO_ELLIPSIS);
}

}  // namespace pane
