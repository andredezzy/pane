// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#import <AppKit/AppKit.h>

#include <cmath>

namespace pane {

// No contrast theme forces the Mac's colors: it has none of Windows'.
void AddForcedColors(ui::ColorMixer& mixer) {}

// A field's selected text keeps the field's own color, as an NSTextField's.
std::optional<SkColor> SystemSelectedTextColor() {
  return std::nullopt;
}

SkColor SystemTextSelection(bool dark, bool key_window) {
  __block SkColor color = SK_ColorTRANSPARENT;
  NSAppearance* appearance = [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
  [appearance performAsCurrentDrawingAppearance:^{
    NSColor* system = key_window
                          ? NSColor.selectedTextBackgroundColor
                          : NSColor.unemphasizedSelectedTextBackgroundColor;
    NSColor* srgb = [system colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!srgb) {
      return;
    }
    const auto channel = [](CGFloat value) {
      return static_cast<U8CPU>(std::lround(value * 255));
    };
    color = SkColorSetARGB(
        channel(srgb.alphaComponent), channel(srgb.redComponent),
        channel(srgb.greenComponent), channel(srgb.blueComponent));
  }];
  return color;
}

}  // namespace pane
