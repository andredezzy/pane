// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include <optional>
#include <string>
#include <vector>

#include "base/no_destructor.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "pane/app/data_launch.h"
#include "pane/browser/engine_trace.h"
#include "pane/model/mac/data_volume.h"
#include "pane/ui/app/notification_center_mac.h"

namespace pane {

namespace {

// This data's profiles, as the launch read them.
std::vector<std::string>& DataProfileIds() {
  static base::NoDestructor<std::vector<std::string>> ids;
  return *ids;
}

// What Chrome's bridge asks as it is made, at the initial profile's load.
std::vector<std::string> LeftoverNotificationProfileIds() {
  PANE_TRACE("engine", "PaneEngine.leftoverNotificationsCleared",
             base::DictValue().Set("profiles",
                                   static_cast<int>(DataProfileIds().size())));
  return DataProfileIds();
}

}  // namespace

// static
void BrowserMainExtraParts::UsePanesWords() {}

// Nothing to start: the ids are read as the initial profile's load nears.
// static
void BrowserMainExtraParts::StartClearingLeftoverNotifications() {}

// A crash closes nothing, so its notifications stayed in macOS's centers, a
// site's words readable there without the PIN, until that profile next
// closed (the user's call, 2026-11-19; ADR 0109). As this process starts, no
// notification of this data's profiles there is its own: Chrome's bridge,
// made at the initial profile's load, closes each profile's by its id in
// both centers, banners' and the alerts helper's, as each service starts
// (patch 0004, notification_platform_bridge_mac.cc), so each service's own
// list drops them (OnNotificationsClosed) and nothing is reported closed to
// a site. The ids are the data's profiles' as the engine's folder names their
// links into the volume, which stay while it is detached: read before the
// key opens, with no profile loaded, and never another copy's, whose data
// has profiles of its own (ADR 0012).
// static
void BrowserMainExtraParts::ClearLeftoverNotifications() {
  DataProfileIds() =
      model::DataVolume(DataDirectory(), std::nullopt).ProfileIds();
  g_pane_notification_profile_ids = &LeftoverNotificationProfileIds;
}

// Chrome's AppController keeps the process alive without windows. ⌘Q quits
// at once, as it did in the Electron app: Chrome asks to hold it (the "Warn
// Before Quitting" pref, on by default), in a panel of its own. At quit,
// each profile's notifications leave macOS's centers as Chrome's teardown
// closes the profile (NotificationPlatformBridgeMac::DisplayServiceShutDown,
// by its id), which Windows' half does for its center by Pane's
// AppUserModelID.
// static
void BrowserMainExtraParts::StartPlatform() {
  g_browser_process->local_state()->SetBoolean(prefs::kConfirmToQuitEnabled,
                                               false);
}

}  // namespace pane
