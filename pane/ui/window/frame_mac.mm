// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

#import <AppKit/AppKit.h>

#include "base/strings/sys_string_conversions.h"
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
  // Measured once: AppKit asks at every title and every step of a resize,
  // and each measure builds a close button (a tenth of a millisecond).
  static const CGFloat height = [] {
    NSButton* close = [NSWindow standardWindowButton:NSWindowCloseButton
                                        forStyleMask:NSWindowStyleMaskTitled];
    return 2 * kLightsInset + NSHeight(close.frame);
  }();
  return height;
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
    ViewsWindowForPane* window = [[ViewsWindowForPane alloc]
        initWithContentRect:ui::kWindowSizeDeterminedLater
                  styleMask:params->style_mask
                    backing:NSBackingStoreBuffered
                      defer:NO];
    // What Chromium's own window creation sets from these params
    // (NativeWidgetNSWindowBridge::CreateNSWindow), which a window class of
    // Pane's goes without: the widget, not AppKit, releases the window; the
    // title bar is transparent and its title hidden, so presses there reach
    // the views under it, as the Swift app's did (ADR 0036); and the window
    // animates in as a document's.
    window.releasedWhenClosed = NO;
    window.titlebarAppearsTransparent = params->titlebar_appears_transparent;
    if (params->window_title_hidden) {
      window.titleVisibility = NSWindowTitleHidden;
    }
    if (params->animation_enabled) {
      window.animationBehavior = NSWindowAnimationBehaviorDocumentWindow;
    }
    // Pane restores its own session; AppKit's restoration is off (ADR 0016).
    window.restorable = NO;
    return window;
  }
};

}  // namespace

views::NativeWidget* CreateFrame(views::Widget* widget) {
  return new Frame(widget);
}

namespace {

// As NSWindow asks it, of its frame view, the top of its views: a point in
// the window's own coordinates, its title bar included.
NSView* HitView(NSWindow* window, const gfx::Point& point) {
  return [window.contentView.superview
      hitTest:NSMakePoint(point.x(), NSHeight(window.frame) - point.y())];
}

}  // namespace

std::string PlatformViewAt(views::Widget* widget, const gfx::Point& point) {
  NSView* hit = HitView(widget->GetNativeWindow().GetNativeNSWindow(), point);
  return hit ? base::SysNSStringToUTF8(NSStringFromClass([hit class]))
             : std::string();
}

PressTarget PressTargetAt(views::Widget* widget, const gfx::Point& point) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content = window.contentView;
  NSView* hit = HitView(window, point);
  if (hit == content) {
    return PressTarget::kViews;
  }
  for (NSView* view = hit; view; view = view.superview) {
    if ([view isKindOfClass:NSClassFromString(@"RenderWidgetHostViewCocoa")]) {
      return PressTarget::kPage;
    }
  }
  // Views passed it on (HTCAPTION): the frame view, or the title bar's own
  // views over it, take it, and a press there moves the window
  // (native_widget_mac_nswindow.mm, -cr_mouseDownOnFrameView:).
  if (hit && ![hit isDescendantOf:content] &&
      ![hit isKindOfClass:[NSControl class]]) {
    return PressTarget::kDrag;
  }
  return PressTarget::kOther;
}

void KeepFrame(views::Widget* widget) {
  [widget->GetNativeWindow().GetNativeNSWindow()
      setFrameAutosaveName:@"PaneMainWindow"];
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
