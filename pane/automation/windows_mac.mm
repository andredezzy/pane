// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/windows.h"

#import <AppKit/AppKit.h>

#include "base/strings/sys_string_conversions.h"
#include "ui/views/widget/widget.h"

namespace pane {

base::ListValue NativeWindows(views::Widget* main) {
  NSWindow* pane = main && !main->IsClosed()
                       ? main->GetNativeWindow().GetNativeNSWindow()
                       : nil;
  base::ListValue windows;
  for (NSWindow* window in NSApp.windows) {
    if (!window.visible) {
      continue;
    }
    NSString* type = NSStringFromClass([window class]);
    const char* role = "other";
    if (window == pane) {
      role = "main";
    } else if (pane && window == pane.attachedSheet) {
      role = "sheet";
    } else if (window == NSApp.modalWindow) {
      role = "modal";
    } else if ([window.identifier isEqualToString:@"pane.devtools"]) {
      role = "devtools";
    } else if ([type hasPrefix:@"_NSFullScreenTransition"] ||
               [type hasPrefix:@"NSToolbarFullScreenWindow"]) {
      // AppKit's own: the full screen animation, and the title bar it
      // reveals at the top of a full screen window.
      role = "system";
    }
    windows.Append(base::DictValue()
                       .Set("role", role)
                       .Set("class", base::SysNSStringToUTF8(type))
                       .Set("title", base::SysNSStringToUTF8(window.title))
                       .Set("frame", base::SysNSStringToUTF8(
                                         NSStringFromRect(window.frame))));
  }
  return windows;
}

}  // namespace pane
