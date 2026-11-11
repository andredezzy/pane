// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/interaction_log.h"

#include <os/signpost.h>

#include <algorithm>

namespace pane {

namespace {

// The Swift app's channel: Pane's subsystem, Points of Interest.
os_log_t InteractionsLog() {
  static os_log_t log = os_log_create("xyz.roundedcorners.pane",
                                      OS_LOG_CATEGORY_POINTS_OF_INTEREST);
  return log;
}

}  // namespace

// An interval's end: how long before it its frame went out, in microseconds.
// A macro, since os_signpost takes its format as a literal.
#define PANE_INTERVAL_END_FORMAT \
  "late=%{public}lld commit=%{public}lld glass=%{public}lld"

uint64_t BeginInteractionInterval(Interaction interaction) {
  os_log_t log = InteractionsLog();
  const os_signpost_id_t id = os_signpost_id_generate(log);
  // os_signpost names its intervals by literal strings.
  switch (interaction) {
    case Interaction::kSelectTab:
      os_signpost_interval_begin(log, id, "Select tab");
      break;
    case Interaction::kNewTab:
      os_signpost_interval_begin(log, id, "New tab");
      break;
    case Interaction::kFocusAddressField:
      os_signpost_interval_begin(log, id, "Focus address field");
      break;
    case Interaction::kClickAddressField:
      os_signpost_interval_begin(log, id, "Click address field");
      break;
    case Interaction::kOpenProfileSheet:
      os_signpost_interval_begin(log, id, "Open profile sheet");
      break;
    case Interaction::kChoosePlatform:
      os_signpost_interval_begin(log, id, "Choose platform");
      break;
    case Interaction::kOpenSwitcher:
      os_signpost_interval_begin(log, id, "Open switcher");
      break;
    case Interaction::kWakeTab:
      os_signpost_interval_begin(log, id, "Wake tab");
      break;
    case Interaction::kOpenSettings:
      os_signpost_interval_begin(log, id, "Open settings");
      break;
    case Interaction::kChangeTheme:
      os_signpost_interval_begin(log, id, "Change theme");
      break;
    case Interaction::kOpenMenu:
      os_signpost_interval_begin(log, id, "Open menu");
      break;
    case Interaction::kOpenAppIconPicker:
      os_signpost_interval_begin(log, id, "Open app icon picker");
      break;
    case Interaction::kChooseAppIcon:
      os_signpost_interval_begin(log, id, "Choose app icon");
      break;
    case Interaction::kOpenQuitDialog:
      os_signpost_interval_begin(log, id, "Open quit dialog");
      break;
    case Interaction::kShowPageDialog:
      os_signpost_interval_begin(log, id, "Page dialog");
      break;
    case Interaction::kShowPermissionPrompt:
      os_signpost_interval_begin(log, id, "Permission prompt");
      break;
    case Interaction::kShowFindBar:
      os_signpost_interval_begin(log, id, "Show find bar");
      break;
    case Interaction::kFindStep:
      os_signpost_interval_begin(log, id, "Find step");
      break;
    case Interaction::kShowContextMenu:
      os_signpost_interval_begin(log, id, "Context menu");
      break;
    case Interaction::kOpenDevTools:
      os_signpost_interval_begin(log, id, "Open developer tools");
      break;
  }
  return id;
}

void EndInteractionInterval(Interaction interaction,
                            uint64_t interval,
                            const FrameTimes& frame) {
  os_log_t log = InteractionsLog();
  // late=: how long the end came after the screen as the GPU process
  // predicts it, 0 while that is still ahead (all an earlier build says);
  // commit=: after the frame's commit to Core Animation; glass=: after the
  // prediction, signed.
  const long long late_us =
      std::max(frame.presented, base::TimeDelta()).InMicroseconds();
  const long long committed_us = frame.committed.InMicroseconds();
  const long long presented_us = frame.presented.InMicroseconds();
  switch (interaction) {
    case Interaction::kSelectTab:
      os_signpost_interval_end(log, interval, "Select tab",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kNewTab:
      os_signpost_interval_end(log, interval, "New tab",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kFocusAddressField:
      os_signpost_interval_end(log, interval, "Focus address field",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kClickAddressField:
      os_signpost_interval_end(log, interval, "Click address field",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenProfileSheet:
      os_signpost_interval_end(log, interval, "Open profile sheet",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kChoosePlatform:
      os_signpost_interval_end(log, interval, "Choose platform",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenSwitcher:
      os_signpost_interval_end(log, interval, "Open switcher",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kWakeTab:
      os_signpost_interval_end(log, interval, "Wake tab",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenSettings:
      os_signpost_interval_end(log, interval, "Open settings",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kChangeTheme:
      os_signpost_interval_end(log, interval, "Change theme",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenMenu:
      os_signpost_interval_end(log, interval, "Open menu",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenAppIconPicker:
      os_signpost_interval_end(log, interval, "Open app icon picker",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kChooseAppIcon:
      os_signpost_interval_end(log, interval, "Choose app icon",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenQuitDialog:
      os_signpost_interval_end(log, interval, "Open quit dialog",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kShowPageDialog:
      os_signpost_interval_end(log, interval, "Page dialog",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kShowPermissionPrompt:
      os_signpost_interval_end(log, interval, "Permission prompt",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kShowFindBar:
      os_signpost_interval_end(log, interval, "Show find bar",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kFindStep:
      os_signpost_interval_end(log, interval, "Find step",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kShowContextMenu:
      os_signpost_interval_end(log, interval, "Context menu",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
    case Interaction::kOpenDevTools:
      os_signpost_interval_end(log, interval, "Open developer tools",
                               PANE_INTERVAL_END_FORMAT, late_us, committed_us,
                               presented_us);
      break;
  }
}

#undef PANE_INTERVAL_END_FORMAT

}  // namespace pane
