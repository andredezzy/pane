// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BROWSER_CONTEXT_INTERNAL_H_
#define PANE_BROWSER_BROWSER_CONTEXT_INTERNAL_H_

#import "pane/public/PaneBrowserContext.h"

class Profile;

@interface PaneBrowserContext ()

// Holds `profile` loaded (a profile keep-alive) until -shutDown.
- (instancetype)initWithProfile:(Profile*)profile
                     identifier:(NSString*)identifier;

// Nil after -shutDown.
@property(readonly) Profile* profile;

// Releases the profile (its keep-alive and download reporting). Called at
// engine shutdown and before the profile is deleted.
- (void)shutDown;

@end

#endif  // PANE_BROWSER_BROWSER_CONTEXT_INTERNAL_H_
