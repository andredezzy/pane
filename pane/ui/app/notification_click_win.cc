// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/notification_click.h"

#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/notifications/notification_platform_bridge.h"
#include "chrome/browser/notifications/win/notification_launch_id.h"
#include "chrome/common/chrome_switches.h"

// Windows': a toast's activation comes as a launch (notification_helper.exe,
// notification_activator.cc), which Pane reads as Chrome's own handler does.

namespace pane {

bool CarriesNotificationClick(const base::CommandLine& command_line) {
  return command_line.HasSwitch(switches::kNotificationLaunchId);
}

// As NotificationPlatformBridgeWin::HandleActivation reads it.
std::optional<NotificationClick> NotificationClickOf(
    const base::CommandLine& command_line) {
  if (!CarriesNotificationClick(command_line)) {
    return std::nullopt;
  }
  const NotificationLaunchId launch_id(base::WideToUTF8(
      command_line.GetSwitchValueNative(switches::kNotificationLaunchId)));
  if (!launch_id.is_valid() || launch_id.incognito()) {
    return std::nullopt;
  }
  NotificationClick click;
  if (launch_id.is_for_dismiss_button()) {
    click.operation = NotificationOperation::kClose;
  } else if (launch_id.is_for_context_menu()) {
    click.operation = NotificationOperation::kSettings;
  } else {
    click.operation = NotificationOperation::kClick;
  }
  click.type = launch_id.notification_type();
  click.origin = launch_id.origin_url();
  click.notification_id = launch_id.notification_id();
  click.profile_id =
      NotificationPlatformBridge::GetProfileBaseNameFromProfileId(
          launch_id.profile_id())
          .AsUTF8Unsafe();
  if (launch_id.button_index() != -1) {
    click.action_index = launch_id.button_index();
  }
  const std::wstring reply =
      command_line.GetSwitchValueNative(switches::kNotificationInlineReply);
  if (!reply.empty()) {
    click.reply = base::WideToUTF16(reply);
  }
  return click;
}

}  // namespace pane
