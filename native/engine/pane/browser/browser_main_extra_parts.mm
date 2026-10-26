// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#import "pane/browser/browser_application_internal.h"

namespace pane {

BrowserMainExtraParts::BrowserMainExtraParts() = default;

BrowserMainExtraParts::~BrowserMainExtraParts() = default;

void BrowserMainExtraParts::PostBrowserStart() {
  [PaneBrowserApplication.sharedApplication engineDidStart];
}

void BrowserMainExtraParts::PostMainMessageLoopRun() {
  [PaneBrowserApplication.sharedApplication engineWillShutDown];
}

}  // namespace pane
