// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

#import <AppKit/AppKit.h>

#include "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
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

// Whether a press at `location` falls on a page's NSView that a view of
// Pane's covers (a sheet, its scrim, the find bar). The page draws in the
// window's compositor, under Views' layers (SetParentUiLayer), but AppKit
// gives a press to the frontmost NSView, the page's, whatever Views draws
// over it there: Views' own answer for the point is then the one to keep.
bool ViewsCoverPageAt(NSWindow* window, NSPoint location) {
  NSView* content = window.contentView;
  NSView* hit = [content.superview
      hitTest:[content.superview convertPoint:location fromView:nil]];
  if (!hit || hit == content || ![hit isDescendantOf:content]) {
    return false;
  }
  views::Widget* widget =
      views::Widget::GetWidgetForNativeWindow(gfx::NativeWindow(window));
  if (!widget || !widget->GetRootView()) {
    return false;
  }
  const NSPoint in_content = [content convertPoint:location fromView:nil];
  const gfx::Point point(in_content.x,
                         content.isFlipped
                             ? in_content.y
                             : NSHeight(content.bounds) - in_content.y);
  for (views::View* view =
           widget->GetRootView()->GetEventHandlerForPoint(point);
       view; view = view->parent()) {
    if (views::IsViewClass<views::NativeViewHost>(view)) {
      return false;
    }
  }
  return true;
}

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

@implementation ViewsWindowForPane {
  // A press Views took over a page, until its release.
  BOOL _viewsHasMouse;
}

// Presses, moves, and scrolls over a page that Views covers go to Views, as
// they would where no page is.
- (void)sendEvent:(NSEvent*)event {
  NSView* content = self.contentView;
  switch (event.type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
    case NSEventTypeOtherMouseDown:
      _viewsHasMouse = ViewsCoverPageAt(self, event.locationInWindow);
      if (_viewsHasMouse) {
        event.type == NSEventTypeLeftMouseDown ? [content mouseDown:event]
        : event.type == NSEventTypeRightMouseDown
            ? [content rightMouseDown:event]
            : [content otherMouseDown:event];
        return;
      }
      break;
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
      if (_viewsHasMouse) {
        event.type == NSEventTypeLeftMouseDragged ? [content mouseDragged:event]
        : event.type == NSEventTypeRightMouseDragged
            ? [content rightMouseDragged:event]
            : [content otherMouseDragged:event];
        return;
      }
      break;
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseUp:
      if (_viewsHasMouse) {
        _viewsHasMouse = NO;
        event.type == NSEventTypeLeftMouseUp    ? [content mouseUp:event]
        : event.type == NSEventTypeRightMouseUp ? [content rightMouseUp:event]
                                                : [content otherMouseUp:event];
        return;
      }
      break;
    case NSEventTypeMouseMoved:
      if (ViewsCoverPageAt(self, event.locationInWindow)) {
        [content mouseMoved:event];
        return;
      }
      break;
    case NSEventTypeScrollWheel:
      if (ViewsCoverPageAt(self, event.locationInWindow)) {
        [content scrollWheel:event];
        return;
      }
      break;
    default:
      break;
  }
  [super sendEvent:event];
}

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

}  // namespace pane
