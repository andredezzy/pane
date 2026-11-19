// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace pane {

// static
void BrowserMainExtraParts::UsePanesWords() {}

// Nothing yet: Chrome's bridge takes a crash's notifications out of macOS's
// center at that profile's next close, by its id, or at the next quit
// (NotificationPlatformBridgeMac). Before the PIN no profile of the crash's
// is loaded, every close of the bridge's takes one, and an alert is the
// alerts helper's, in a center of its own that only a process of the
// helper's bundle reaches (ADR 0109).
// static
void BrowserMainExtraParts::ClearLeftoverNotifications() {}

// Chrome's AppController keeps the process alive without windows. ⌘Q quits
// at once, as it did in the Electron app: Chrome asks to hold it (the "Warn
// Before Quitting" pref, on by default), in a panel of its own. Chrome's own
// bridge takes every notification out of macOS's center at quit
// (~NotificationPlatformBridgeMac), which Windows' half does for its center.
// static
void BrowserMainExtraParts::StartPlatform() {
  g_browser_process->local_state()->SetBoolean(prefs::kConfirmToQuitEnabled,
                                               false);
}

}  // namespace pane
