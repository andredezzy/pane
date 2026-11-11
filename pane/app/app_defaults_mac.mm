// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/app_defaults.h"

#import <Foundation/Foundation.h>

namespace pane {

// NSUserDefaults' registration domain, which CFPreferences has no call for.
void RegisterAppDefaults() {
  [NSUserDefaults.standardUserDefaults registerDefaults:@{
    @"ApplePersistenceIgnoreState" : @YES,
    @"AppleFontSmoothing" : @0,
    @"NSQuitAlwaysKeepsWindows" : @NO,
  }];
}

}  // namespace pane
