// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/interaction_log.h"

#include <os/signpost.h>

namespace pane {

namespace {

// The Swift app's channel: Pane's subsystem, Points of Interest.
os_log_t InteractionsLog() {
  static os_log_t log = os_log_create("xyz.roundedcorners.pane",
                                      OS_LOG_CATEGORY_POINTS_OF_INTEREST);
  return log;
}

}  // namespace

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
    case Interaction::kOpenProfileSheet:
      os_signpost_interval_begin(log, id, "Open profile sheet");
      break;
    case Interaction::kChoosePlatform:
      os_signpost_interval_begin(log, id, "Choose platform");
      break;
  }
  return id;
}

void EndInteractionInterval(Interaction interaction, uint64_t interval) {
  os_log_t log = InteractionsLog();
  switch (interaction) {
    case Interaction::kSelectTab:
      os_signpost_interval_end(log, interval, "Select tab");
      break;
    case Interaction::kNewTab:
      os_signpost_interval_end(log, interval, "New tab");
      break;
    case Interaction::kFocusAddressField:
      os_signpost_interval_end(log, interval, "Focus address field");
      break;
    case Interaction::kOpenProfileSheet:
      os_signpost_interval_end(log, interval, "Open profile sheet");
      break;
    case Interaction::kChoosePlatform:
      os_signpost_interval_end(log, interval, "Choose platform");
      break;
  }
}

}  // namespace pane
