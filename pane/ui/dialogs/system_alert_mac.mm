// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/system_alert.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "base/functional/callback.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/views/widget/widget.h"

namespace pane {

// As SwiftUI's alert makes it (read off one, 2026-11-09): the action's button
// first, so rightmost, destructive and without Return, since a destructive
// action is never the default; Cancel to its left, on Esc. macOS draws the
// rest: the sheet centered on the window, 260 points wide, its text centered.
void ShowSystemAlert(views::Widget* parent,
                     const SystemAlert& alert,
                     base::OnceCallback<void(bool chosen)> done) {
  NSWindow* window =
      parent ? parent->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window || window.attachedSheet) {
    std::move(done).Run(false);
    return;
  }
  NSAlert* sheet = [[NSAlert alloc] init];
  sheet.messageText = base::SysUTF16ToNSString(alert.title);
  sheet.informativeText = base::SysUTF16ToNSString(alert.message);
  NSButton* action =
      [sheet addButtonWithTitle:base::SysUTF16ToNSString(alert.action)];
  action.hasDestructiveAction = YES;
  action.keyEquivalent = @"";
  NSButton* cancel =
      [sheet addButtonWithTitle:base::SysUTF16ToNSString(alert.cancel)];
  cancel.keyEquivalent = @"\e";

  __block base::OnceCallback<void(bool)> answer = std::move(done);
  [sheet beginSheetModalForWindow:window
                completionHandler:^(NSModalResponse response) {
                  std::move(answer).Run(response == NSAlertFirstButtonReturn);
                }];
}

}  // namespace pane
