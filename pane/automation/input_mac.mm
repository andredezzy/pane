// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/input.h"

#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

#include <algorithm>
#include <map>

#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The keys scenarios type, by the US layout's virtual codes.
const std::map<std::string, uint16_t>& KeyCodes() {
  static const base::NoDestructor<std::map<std::string, uint16_t>> codes({
      {"a", kVK_ANSI_A},           {"f", kVK_ANSI_F},
      {"i", kVK_ANSI_I},           {"k", kVK_ANSI_K},
      {"l", kVK_ANSI_L},           {"p", kVK_ANSI_P},
      {"q", kVK_ANSI_Q},           {"r", kVK_ANSI_R},
      {"t", kVK_ANSI_T},           {"u", kVK_ANSI_U},
      {"w", kVK_ANSI_W},           {"y", kVK_ANSI_Y},
      {"0", kVK_ANSI_0},           {"1", kVK_ANSI_1},
      {"2", kVK_ANSI_2},           {"3", kVK_ANSI_3},
      {"4", kVK_ANSI_4},           {"5", kVK_ANSI_5},
      {"6", kVK_ANSI_6},           {"7", kVK_ANSI_7},
      {"8", kVK_ANSI_8},           {"9", kVK_ANSI_9},
      {"=", kVK_ANSI_Equal},       {"-", kVK_ANSI_Minus},
      {"[", kVK_ANSI_LeftBracket}, {"]", kVK_ANSI_RightBracket},
      {",", kVK_ANSI_Comma},       {"\t", kVK_Tab},
      {"\r", kVK_Return},          {"\x1b", kVK_Escape},
  });
  return *codes;
}

NSEventModifierFlags Flags(const std::vector<std::string>& modifiers) {
  NSEventModifierFlags flags = 0;
  for (const std::string& name : modifiers) {
    if (name == "command") {
      flags |= NSEventModifierFlagCommand;
    } else if (name == "shift") {
      flags |= NSEventModifierFlagShift;
    } else if (name == "control") {
      flags |= NSEventModifierFlagControl;
    } else if (name == "option") {
      flags |= NSEventModifierFlagOption;
    } else if (name == "capsLock") {
      flags |= NSEventModifierFlagCapsLock;
    }
  }
  return flags;
}

NSInteger WindowNumber(views::Widget* widget) {
  return widget ? widget->GetNativeWindow().GetNativeNSWindow().windowNumber
                : 0;
}

}  // namespace

bool PostKey(views::Widget* widget,
             const std::string& key,
             const std::vector<std::string>& modifiers) {
  const auto code = KeyCodes().find(key);
  if (code == KeyCodes().end()) {
    return false;
  }
  const NSEventModifierFlags flags = Flags(modifiers);
  if (flags) {
    PostModifiers(widget, modifiers);
  }
  // As typed: Shift makes a letter uppercase.
  NSString* characters = base::SysUTF8ToNSString(key);
  if (flags & NSEventModifierFlagShift) {
    characters = characters.uppercaseString;
  }
  for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
    NSEvent* event =
        [NSEvent keyEventWithType:type
                               location:NSZeroPoint
                          modifierFlags:flags
                              timestamp:NSProcessInfo.processInfo.systemUptime
                           windowNumber:WindowNumber(widget)
                                context:nil
                             characters:characters
            charactersIgnoringModifiers:characters
                              isARepeat:NO
                                keyCode:code->second];
    [NSApp postEvent:event atStart:NO];
  }
  return true;
}

void PostClick(views::Widget* widget, int x, int y, int clicks) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
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

void PostModifiers(views::Widget* widget,
                   const std::vector<std::string>& modifiers) {
  NSEvent* event =
      [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                             location:NSZeroPoint
                        modifierFlags:Flags(modifiers)
                            timestamp:NSProcessInfo.processInfo.systemUptime
                         windowNumber:WindowNumber(widget)
                              context:nil
                           characters:@""
          charactersIgnoringModifiers:@""
                            isARepeat:NO
                              keyCode:0];
  [NSApp postEvent:event atStart:NO];
}

}  // namespace pane
