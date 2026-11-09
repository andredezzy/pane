// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

#import <AppKit/AppKit.h>

#include "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/widget/native_widget_mac.h"
#include "ui/views/widget/widget.h"

// AppKit's own, which Chrome's browser window overrides the same way
// (browser_native_widget_window_mac.mm).
@interface NSWindow (ViewsFrameForPane)
+ (Class)frameViewClassForStyleMask:(NSUInteger)windowStyle;
@end

@interface NSThemeFrame (ViewsFrameForPane)
- (CGFloat)_titlebarHeight;
- (CGFloat)_minXTitlebarWidgetInset;
- (void)setStyleMask:(NSUInteger)styleMask;
@end

namespace {

// The traffic lights' corner: 12 points from the window's left and top,
// in the sidebar's header row.
constexpr CGFloat kLightsInset = 12;

}  // namespace

// A title bar as tall as puts the lights, centered in it, 12 points from the
// top; in full screen, AppKit's own.
@interface ViewsTitledFrameForPane : NativeWidgetMacNSWindowTitledFrame
@end

@implementation ViewsTitledFrameForPane {
  BOOL _inFullScreen;
}

- (CGFloat)_titlebarHeight {
  if (_inFullScreen) {
    return [super _titlebarHeight];
  }
  NSButton* close = [NSWindow standardWindowButton:NSWindowCloseButton
                                      forStyleMask:NSWindowStyleMaskTitled];
  return 2 * kLightsInset + NSHeight(close.frame);
}

- (CGFloat)_minXTitlebarWidgetInset {
  return kLightsInset;
}

- (BOOL)_shouldCenterTrafficLights {
  return YES;
}

- (void)setStyleMask:(NSUInteger)styleMask {
  _inFullScreen = (styleMask & NSWindowStyleMaskFullScreen) != 0;
  [super setStyleMask:styleMask];
}

@end

@interface ViewsWindowForPane : NativeWidgetMacNSWindow
@end

@implementation ViewsWindowForPane

+ (Class)frameViewClassForStyleMask:(NSUInteger)windowStyle {
  // NSThemeFrame's subclasses are nil if AppKit no longer has it.
  if ([ViewsTitledFrameForPane class]) {
    return [ViewsTitledFrameForPane class];
  }
  return [super frameViewClassForStyleMask:windowStyle];
}

@end

namespace pane {

namespace {

class Frame : public views::NativeWidgetMac {
 public:
  explicit Frame(views::Widget* widget) : views::NativeWidgetMac(widget) {}

 protected:
  // views::NativeWidgetMac:
  void PopulateCreateWindowParams(
      const views::Widget::InitParams& widget_params,
      remote_cocoa::mojom::CreateWindowParams* params) override {
    params->style_mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                         NSWindowStyleMaskMiniaturizable |
                         NSWindowStyleMaskResizable |
                         NSWindowStyleMaskFullSizeContentView;
    params->titlebar_appears_transparent = true;
    params->window_title_hidden = true;
    params->animation_enabled = true;
  }

  NativeWidgetMacNSWindow* CreateNSWindow(
      const remote_cocoa::mojom::CreateWindowParams* params) override {
    return [[ViewsWindowForPane alloc]
        initWithContentRect:ui::kWindowSizeDeterminedLater
                  styleMask:params->style_mask
                    backing:NSBackingStoreBuffered
                      defer:NO];
  }
};

}  // namespace

views::NativeWidget* CreateFrame(views::Widget* widget) {
  return new Frame(widget);
}

PressTarget PressTargetAt(views::Widget* widget, const gfx::Point& point) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content = window.contentView;
  // -hitTest: takes a point in its receiver's superview's coordinates, as
  // NSWindow asks it.
  NSView* hit = [content
      hitTest:[content.superview
                  convertPoint:NSMakePoint(point.x(),
                                           NSHeight(window.frame) - point.y())
                      fromView:nil]];
  if (hit == content) {
    return PressTarget::kViews;
  }
  for (NSView* view = hit; view; view = view.superview) {
    if ([view isKindOfClass:NSClassFromString(@"RenderWidgetHostViewCocoa")]) {
      return PressTarget::kPage;
    }
  }
  return PressTarget::kOther;
}

std::vector<gfx::Rect> WindowControlFrames(views::Widget* widget) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  std::vector<gfx::Rect> frames;
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    if (NSButton* button = [window standardWindowButton:type]) {
      const NSRect frame = [button convertRect:button.bounds toView:nil];
      frames.emplace_back(NSMinX(frame), NSHeight(window.frame) - NSMaxY(frame),
                          NSWidth(frame), NSHeight(frame));
    }
  }
  return frames;
}

}  // namespace pane
