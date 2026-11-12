// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace pane {

// Chrome's AppController keeps the process alive without windows. ⌘Q quits
// at once, as it did in the Electron app: Chrome asks to hold it (the "Warn
// Before Quitting" pref, on by default), in a panel of its own.
// static
void BrowserMainExtraParts::StartPlatform() {
  g_browser_process->local_state()->SetBoolean(prefs::kConfirmToQuitEnabled,
                                               false);
}

}  // namespace pane
