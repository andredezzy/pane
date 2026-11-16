// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/system.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <stdlib.h>

#include "base/path_service.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/notifications/mac/notification_utils.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/notifications/notification_constants.h"
#include "chrome/common/notifications/notification_operation.h"
#include "chrome/services/mac_notifications/public/mojom/mac_notifications.mojom.h"
#include "url/gurl.h"

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

bool ClickSystemNotification(const std::string& profile_id,
                             const std::string& id,
                             const std::string& origin,
                             bool persistent) {
  auto info = mac_notifications::mojom::NotificationActionInfo::New();
  info->meta = mac_notifications::mojom::NotificationMetadata::New();
  info->meta->id = mac_notifications::mojom::NotificationIdentifier::New(
      id, mac_notifications::mojom::ProfileIdentifier::New(
              profile_id, /*incognito=*/false));
  info->meta->type = static_cast<int32_t>(
      persistent ? NotificationHandler::Type::WEB_PERSISTENT
                 : NotificationHandler::Type::WEB_NON_PERSISTENT);
  info->meta->origin_url = GURL(origin);
  // The browser's own, which the response must name to be heard
  // (VerifyMacNotificationData).
  info->meta->user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA).value();
  info->operation = NotificationOperation::kClick;
  info->button_index = kNotificationInvalidButtonIndex;
  ProcessMacNotificationResponse(mac_notifications::NotificationStyle::kBanner,
                                 std::move(info));
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
