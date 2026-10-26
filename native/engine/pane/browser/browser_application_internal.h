// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BROWSER_APPLICATION_INTERNAL_H_
#define PANE_BROWSER_BROWSER_APPLICATION_INTERNAL_H_

#import "pane/public/PaneBrowserApplication.h"

@interface PaneBrowserApplication ()

// Called once by BrowserMainExtraParts when the engine finished starting.
- (void)engineDidStart;

// Called once by BrowserMainExtraParts after the main loop ends, before
// profiles are destroyed: closes every page and releases every profile.
- (void)engineWillShutDown;

@end

#endif  // PANE_BROWSER_BROWSER_APPLICATION_INTERNAL_H_
