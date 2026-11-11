// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/native_input_mac.h"

#import <AppKit/AppKit.h>

#include <algorithm>

#include "base/strings/sys_string_conversions.h"
#include "pane/automation/input.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

NSWindow* WindowOf(views::Widget* widget) {
  return widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
}

// A key event, its flags NSEvent's, as the hardware's would be.
NSEvent* KeyEvent(views::Widget* widget,
                  NSEventType type,
                  CGEventFlags flags,
                  NSString* characters,
                  NSString* ignoring,
                  uint16_t code) {
  return [NSEvent keyEventWithType:type
                          location:NSZeroPoint
                     modifierFlags:static_cast<NSEventModifierFlags>(flags)
                         timestamp:NSProcessInfo.processInfo.systemUptime
                      windowNumber:WindowOf(widget).windowNumber
                           context:nil
                        characters:characters
       charactersIgnoringModifiers:ignoring
                         isARepeat:NO
                           keyCode:code];
}

}  // namespace

namespace native_input {

void Key(views::Widget* widget,
         bool down,
         CGEventFlags flags,
         const std::string& characters,
         const std::string& ignoring,
         uint16_t code,
         bool to_window) {
  NSEvent* event =
      KeyEvent(widget, down ? NSEventTypeKeyDown : NSEventTypeKeyUp, flags,
               base::SysUTF8ToNSString(characters),
               base::SysUTF8ToNSString(ignoring), code);
  if (to_window) {
    [WindowOf(widget) sendEvent:event];
  } else {
    [NSApp postEvent:event atStart:NO];
  }
}

void FlagsChanged(views::Widget* widget, CGEventFlags flags, uint16_t code) {
  [NSApp
      postEvent:KeyEvent(widget, NSEventTypeFlagsChanged, flags, @"", @"", code)
        atStart:NO];
}

}  // namespace native_input

void PostClick(views::Widget* widget, int x, int y, int clicks) {
  NSWindow* window = WindowOf(widget);
  const NSPoint location = NSMakePoint(x, NSHeight(window.frame) - y);
  for (int count = 1; count <= std::max(clicks, 1); ++count) {
    for (NSEventType type :
         {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
      NSEvent* event =
          [NSEvent mouseEventWithType:type
                             location:location
                        modifierFlags:0
                            timestamp:NSProcessInfo.processInfo.systemUptime
                         windowNumber:window.windowNumber
                              context:nil
                          eventNumber:0
                           clickCount:count
                             pressure:type == NSEventTypeLeftMouseUp ? 0 : 1];
      [NSApp postEvent:event atStart:NO];
    }
  }
}

void PostMove(views::Widget* widget, int x, int y) {
  NSWindow* window = WindowOf(widget);
  const NSPoint location = NSMakePoint(x, NSHeight(window.frame) - y);
  NSEvent* event =
      [NSEvent mouseEventWithType:NSEventTypeMouseMoved
                         location:location
                    modifierFlags:0
                        timestamp:NSProcessInfo.processInfo.systemUptime
                     windowNumber:window.windowNumber
                          context:nil
                      eventNumber:0
                       clickCount:0
                         pressure:0];
  [window sendEvent:event];
  // Views hears the pointer move through its content view's tracking area,
  // which AppKit runs only while the app is active, as a scenario's copy in
  // the background isn't: the move goes to that view as the area would hand
  // it, where the pointer is over it, a part the frame moves the window by
  // included (the area covers the whole view), the frame's own buttons not.
  NSView* content = window.contentView;
  NSView* hit = [content.superview hitTest:location];
  if (hit == content || hit == content.superview) {
    [content mouseMoved:event];
  }
}

void PostScroll(views::Widget* widget, int x, int y, double dy) {
  NSWindow* window = WindowOf(widget);
  const NSPoint on_screen =
      [window convertPointToScreen:NSMakePoint(x, NSHeight(window.frame) - y)];
  const CGFloat top = NSMaxY(NSScreen.screens.firstObject.frame);
  CGEventRef scroll = CGEventCreateScrollWheelEvent2(
      nullptr, kCGScrollEventUnitPixel, 1, static_cast<int32_t>(dy), 0, 0);
  if (!scroll) {
    return;
  }
  CGEventSetLocation(scroll, CGPointMake(on_screen.x, top - on_screen.y));
  NSEvent* event = [NSEvent eventWithCGEvent:scroll];
  CFRelease(scroll);
  if (event) {
    [window sendEvent:event];
  }
}

}  // namespace pane
