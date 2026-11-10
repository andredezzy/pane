// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/frame.h"

#import <AppKit/AppKit.h>

#include <optional>
#include <utility>

#include "base/apple/owned_objc.h"
#include "base/strings/sys_string_conversions.h"
#include "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"
#include "pane/ui/app/commands.h"
#include "ui/base/accelerators/accelerator.h"
#import "ui/base/cocoa/command_dispatcher.h"
#import "ui/base/cocoa/default_command_dispatcher_delegate.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/events/event.h"
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

// The window's key equivalents, ahead of its views and the menu bar: while
// an overlay takes every key, a key the menu bar would run goes nowhere, Quit
// but; any other key goes on to the overlay. Otherwise, as Views' own.
@interface KeyEquivalentsForPane : DefaultCommandDispatcherDelegate
- (instancetype)initWithTakesEveryKey:
    (base::RepeatingCallback<bool()>)takesEveryKey;
@end

@implementation KeyEquivalentsForPane {
  base::RepeatingCallback<bool()> _takesEveryKey;
}

- (instancetype)initWithTakesEveryKey:
    (base::RepeatingCallback<bool()>)takesEveryKey {
  if ((self = [super init])) {
    _takesEveryKey = std::move(takesEveryKey);
  }
  return self;
}

- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  if (_takesEveryKey.Run()) {
    const std::optional<pane::Command> command = pane::CommandForAccelerator(
        ui::Accelerator(ui::KeyEvent(base::apple::OwnedNSEvent(event))));
    if (command && command != pane::Command::kQuit) {
      return ui::PerformKeyEquivalentResult::kDrop;
    }
  }
  return [super prePerformKeyEquivalent:event window:window];
}

@end

@interface ViewsWindowForPane : NativeWidgetMacNSWindow
- (void)setControlChanged:
    (base::RepeatingCallback<void(bool controlDown)>)controlChanged;
@end

@implementation ViewsWindowForPane {
  base::RepeatingCallback<void(bool controlDown)> _controlChanged;
}

- (void)setControlChanged:
    (base::RepeatingCallback<void(bool controlDown)>)controlChanged {
  _controlChanged = std::move(controlChanged);
}

// Every modifier change passes here before the page or Pane's views take it,
// once: one a page left, which Chromium sends back through the window later
// (redispatchKeyEvent:), is no new change, and a Command released before a
// Ctrl-Tab would otherwise come back after it as Ctrl's release.
- (void)sendEvent:(NSEvent*)event {
  if (event.type == NSEventTypeFlagsChanged && _controlChanged &&
      ![[self commandDispatcher] isEventBeingRedispatched:event]) {
    _controlChanged.Run((event.modifierFlags & NSEventModifierFlagControl) !=
                        0);
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
  Frame(views::Widget* widget,
        base::RepeatingCallback<bool()> takes_every_key,
        base::RepeatingCallback<void(bool)> control_changed)
      : views::NativeWidgetMac(widget),
        key_equivalents_([[KeyEquivalentsForPane alloc]
            initWithTakesEveryKey:std::move(takes_every_key)]),
        control_changed_(std::move(control_changed)) {}

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
    // The window holds its dispatcher's delegate weakly; this frame keeps it.
    [window setCommandDispatcherDelegate:key_equivalents_];
    [window setControlChanged:control_changed_];
    return window;
  }

 private:
  KeyEquivalentsForPane* __strong key_equivalents_;
  const base::RepeatingCallback<void(bool)> control_changed_;
};

}  // namespace

views::NativeWidget* CreateFrame(
    views::Widget* widget,
    base::RepeatingCallback<bool()> takes_every_key,
    base::RepeatingCallback<void(bool control_down)> control_changed) {
  return new Frame(widget, std::move(takes_every_key),
                   std::move(control_changed));
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
