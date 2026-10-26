// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BROWSER_CONTEXT_INTERNAL_H_
#define PANE_BROWSER_BROWSER_CONTEXT_INTERNAL_H_

#import "pane/public/PaneBrowserContext.h"

class Profile;

@interface PaneBrowserContext ()

// Holds `profile` loaded (a profile keep-alive) until -shutDown.
- (instancetype)initWithProfile:(Profile*)profile
                     identifier:(NSString*)identifier;

@property(readonly) Profile* profile;

// Closes this context's pages and releases its profile. Called at engine
// shutdown, before Chrome destroys profiles.
- (void)shutDown;

@end

#endif  // PANE_BROWSER_BROWSER_CONTEXT_INTERNAL_H_
