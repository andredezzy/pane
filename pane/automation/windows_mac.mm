// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/windows.h"

#import <AppKit/AppKit.h>

#include "base/strings/sys_string_conversions.h"
#include "pane/ui/extensions/action_popup.h"
#include "pane/ui/page/devtools_window.h"
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
    } else if ([type isEqualToString:@"NSAccessoryViewWindow"]) {
      // The open or save panel's own accessory (Chrome's Format menu), which
      // macOS hosts in a window of its own beside the panel run out of
      // process, a moment longer than the panel as it goes.
      role = "sheet";
    } else if (window == NSApp.modalWindow) {
      role = "modal";
    } else if (views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
                   gfx::NativeWindow(window));
               widget && widget->GetName() == kDevToolsWindowName) {
      role = "devtools";
    } else if (widget && widget->GetName() == kActionPopupWindowName) {
      // An action's popup, as the Swift's NSPopover's window was named.
      role = "popover";
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
                       // What `screencapture -l` takes, for a picture of the
                       // window alone.
                       .Set("number", static_cast<int>(window.windowNumber))
                       .Set("frame", base::SysNSStringToUTF8(
                                         NSStringFromRect(window.frame))));
  }
  return windows;
}

namespace {

NSWindow* AttachedSheet(views::Widget* main) {
  return main && !main->IsClosed()
             ? main->GetNativeWindow().GetNativeNSWindow().attachedSheet
             : nil;
}

// The sheet's labels and buttons, in the order AppKit lays them out.
void Collect(NSView* view,
             NSMutableArray<NSString*>* texts,
             NSMutableArray<NSButton*>* buttons) {
  if ([view isKindOfClass:[NSButton class]]) {
    [buttons addObject:(NSButton*)view];
    return;
  }
  if ([view isKindOfClass:[NSTextField class]] && !view.hidden &&
      ((NSTextField*)view).stringValue.length) {
    [texts addObject:((NSTextField*)view).stringValue];
    return;
  }
  for (NSView* subview in view.subviews) {
    Collect(subview, texts, buttons);
  }
}

}  // namespace

base::Value SheetAlert(views::Widget* main) {
  NSWindow* sheet = AttachedSheet(main);
  if (!sheet) {
    return base::Value();
  }
  NSMutableArray<NSString*>* texts = [NSMutableArray array];
  NSMutableArray<NSButton*>* buttons = [NSMutableArray array];
  Collect(sheet.contentView, texts, buttons);
  base::ListValue titles;
  for (NSButton* button in buttons) {
    titles.Append(base::SysNSStringToUTF8(button.title));
  }
  return base::Value(
      base::DictValue()
          .Set("title", texts.count ? base::SysNSStringToUTF8(texts[0]) : "")
          .Set("message",
               texts.count > 1
                   ? base::SysNSStringToUTF8([[texts
                         subarrayWithRange:NSMakeRange(1, texts.count - 1)]
                         componentsJoinedByString:@"\n"])
                   : "")
          .Set("buttons", std::move(titles)));
}

bool PressSheetButton(views::Widget* main, std::string_view title) {
  for (NSWindow* window : {AttachedSheet(main), NSApp.modalWindow}) {
    if (!window) {
      continue;
    }
    NSMutableArray<NSString*>* texts = [NSMutableArray array];
    NSMutableArray<NSButton*>* buttons = [NSMutableArray array];
    Collect(window.contentView, texts, buttons);
    for (NSButton* button in buttons) {
      if (base::SysNSStringToUTF8(button.title) == title) {
        [button performClick:nil];
        return true;
      }
    }
  }
  return false;
}

base::Value ModalPanel() {
  NSWindow* modal = NSApp.modalWindow;
  if (!modal) {
    return base::Value();
  }
  NSMutableArray<NSString*>* texts = [NSMutableArray array];
  NSMutableArray<NSButton*>* buttons = [NSMutableArray array];
  Collect(modal.contentView, texts, buttons);
  base::ListValue titles;
  for (NSButton* button in buttons) {
    titles.Append(base::SysNSStringToUTF8(button.title));
  }
  return base::Value(base::DictValue()
                         .Set("title", base::SysNSStringToUTF8(modal.title))
                         .Set("buttons", std::move(titles)));
}

}  // namespace pane
