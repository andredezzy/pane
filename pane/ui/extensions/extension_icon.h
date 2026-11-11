// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_EXTENSION_ICON_H_
#define PANE_UI_EXTENSIONS_EXTENSION_ICON_H_

#include <string>

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
class Image;
class RectF;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

namespace pane {

// An extension's icon filling `bounds`, or, without one, the first letter of
// its name on gray (the Swift's ExtensionIcon and ExtensionLetter): white,
// medium, at `letter_size` points, on Palette.letterTile rounded `radius`. An
// icon still loading is there and blank, as the Swift showed it.
void PaintExtensionIcon(gfx::Canvas& canvas,
                        const ui::ColorProvider& colors,
                        const gfx::Image& icon,
                        const std::string& name,
                        const gfx::RectF& bounds,
                        int letter_size,
                        float radius);

// A pinned action's badge at the bottom of `button` (the Swift's
// ActionButton): its text at 9 points, 2 in on each side, 12 tall, on its
// color rounded 4 and ringed by a point of Palette.badgeRing on its edge, its
// trailing edge on the button's and its bottom 1 above. A color the extension
// left unset (transparent) is the Swift's default: the system's red under
// white.
void PaintBadge(gfx::Canvas& canvas,
                const ui::ColorProvider& colors,
                const std::u16string& text,
                SkColor background,
                SkColor text_color,
                const gfx::RectF& button);

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_EXTENSION_ICON_H_
