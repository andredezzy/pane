// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/checkbox.h"

#import <AppKit/AppKit.h>

#include <cmath>
#include <map>
#include <tuple>

#include "base/no_destructor.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace pane {

namespace {

// AppKit's checkbox, its cell drawn offscreen at `scale` in the appearance
// asked, flipped as its control is.
SkBitmap DrawBox(const SystemCheckboxState& state, float scale) {
  NSButton* box = [NSButton checkboxWithTitle:@"" target:nil action:nil];
  box.state = state.checked ? NSControlStateValueOn : NSControlStateValueOff;
  box.enabled = state.enabled;
  box.cell.highlighted = state.pressed;
  NSAppearance* appearance =
      [NSAppearance appearanceNamed:state.dark ? NSAppearanceNameDarkAqua
                                               : NSAppearanceNameAqua];
  box.appearance = appearance;
  const NSRect frame =
      NSMakeRect(0, 0, kSystemCheckboxSize, kSystemCheckboxSize);
  box.frame = frame;
  const NSInteger pixels = std::lround(kSystemCheckboxSize * scale);
  NSBitmapImageRep* rep =
      [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                              pixelsWide:pixels
                                              pixelsHigh:pixels
                                           bitsPerSample:8
                                         samplesPerPixel:4
                                                hasAlpha:YES
                                                isPlanar:NO
                                          colorSpaceName:NSDeviceRGBColorSpace
                                             bytesPerRow:0
                                            bitsPerPixel:0];
  rep.size = frame.size;
  NSGraphicsContext* bitmap =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
  CGContextTranslateCTM(bitmap.CGContext, 0, NSHeight(frame));
  CGContextScaleCTM(bitmap.CGContext, 1, -1);
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext =
      [NSGraphicsContext graphicsContextWithCGContext:bitmap.CGContext
                                              flipped:YES];
  [appearance performAsCurrentDrawingAppearance:^{
    [box.cell drawWithFrame:frame inView:box];
  }];
  [NSGraphicsContext restoreGraphicsState];
  return skia::CGImageToSkBitmap(rep.CGImage);
}

}  // namespace

void PaintSystemCheckbox(gfx::Canvas& canvas,
                         const ui::ColorProvider* colors,
                         const gfx::Rect& bounds,
                         const SystemCheckboxState& state) {
  // Each look at each scale is drawn once.
  using Key = std::tuple<bool, bool, bool, bool, float>;
  static base::NoDestructor<std::map<Key, SkBitmap>> drawn;
  const float scale = canvas.image_scale();
  const Key key(state.checked, state.pressed, state.enabled, state.dark, scale);
  auto found = drawn->find(key);
  if (found == drawn->end()) {
    found = drawn->emplace(key, DrawBox(state, scale)).first;
  }
  canvas.DrawImageInt(gfx::ImageSkia::CreateFromBitmap(found->second, scale),
                      bounds.x(), bounds.y());
}

}  // namespace pane
