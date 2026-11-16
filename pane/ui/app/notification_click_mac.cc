// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/notification_click.h"

#include "base/command_line.h"

// The Mac's: macOS's notification center hands a click to Chrome's own
// handler in the browser (NotificationDispatcherMojo), never as a launch.

namespace pane {

bool CarriesNotificationClick(const base::CommandLine& command_line) {
  return false;
}

std::optional<NotificationClick> NotificationClickOf(
    const base::CommandLine& command_line) {
  return std::nullopt;
}

}  // namespace pane
