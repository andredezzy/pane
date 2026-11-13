// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/system.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <stdlib.h>

#include "base/strings/sys_string_conversions.h"

namespace pane {

bool ChangeSystemTimeZone(const std::string& zone) {
  // The engine's monitor asks ICU, which reads TZ first, when macOS says the
  // zone changed (time_zone_monitor_mac.mm).
  setenv("TZ", zone.c_str(), 1);
  [NSNotificationCenter.defaultCenter
      postNotificationName:NSSystemTimeZoneDidChangeNotification
                    object:nil];
  return true;
}

void RequestEnhancedUserInterface() {
  // Deprecated, and what the engine's application overrides to hear of it.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [NSApp accessibilitySetValue:@YES forAttribute:@"AXEnhancedUserInterface"];
#pragma clang diagnostic pop
}

bool PostSystemEvent(const std::string& event) {
  NSNotificationName name = nil;
  if (event == "screensSleep") {
    name = NSWorkspaceScreensDidSleepNotification;
  } else if (event == "sessionResign") {
    name = NSWorkspaceSessionDidResignActiveNotification;
  } else {
    return false;
  }
  // NSWorkspace's center is the process's own: the user's apps hear none of
  // it.
  [NSWorkspace.sharedWorkspace.notificationCenter
      postNotificationName:name
                    object:NSWorkspace.sharedWorkspace];
  return true;
}

base::expected<std::optional<std::string>, std::string> RunAppleScript(
    const std::string& source) {
  NSAppleScript* script =
      [[NSAppleScript alloc] initWithSource:base::SysUTF8ToNSString(source)];
  NSDictionary<NSString*, id>* error = nil;
  NSAppleEventDescriptor* result =
      script ? [script executeAndReturnError:&error] : nil;
  if (!result) {
    id message = error[NSAppleScriptErrorMessage];
    return base::unexpected(
        "AppleScript: " +
        base::SysNSStringToUTF8(
            message ? [message description]
                    : (error ? error.description : @"no script")));
  }
  NSString* text = result.stringValue;
  if (!text) {
    return std::nullopt;
  }
  return std::optional<std::string>(base::SysNSStringToUTF8(text));
}

}  // namespace pane
