// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "pane/browser/ui/window_host.h"

#import "pane/browser/browser_application_internal.h"

namespace pane {

BrowserMainExtraParts::BrowserMainExtraParts() = default;

BrowserMainExtraParts::~BrowserMainExtraParts() = default;

void BrowserMainExtraParts::PostProfileInit(Profile* profile,
                                            bool is_initial_profile) {
  if (!profile_manager_observation_.IsObserving()) {
    profile_manager_observation_.Observe(
        g_browser_process->profile_manager());
  }
  OnProfileAdded(profile);
}

// The spellchecker hands its dictionaries only to renderers that start after
// it exists. Chrome brings it up with sync; Pane does as the profile arrives,
// before any of its renderers.
void BrowserMainExtraParts::OnProfileAdded(Profile* profile) {
  SpellcheckServiceFactory::GetForContext(profile);
}

void BrowserMainExtraParts::OnProfileManagerDestroying() {
  profile_manager_observation_.Reset();
}

void BrowserMainExtraParts::PostBrowserStart() {
  InstallUIHooks();
  [PaneBrowserApplication.sharedApplication engineDidStart];
}

void BrowserMainExtraParts::PostMainMessageLoopRun() {
  [PaneBrowserApplication.sharedApplication engineWillShutDown];
}

}  // namespace pane
