// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/system.h"

#import <Foundation/Foundation.h>

#include <stdlib.h>

namespace pane {

void ChangeSystemTimeZone(const std::string& zone) {
  // The engine's monitor asks ICU, which reads TZ first, when macOS says the
  // zone changed (time_zone_monitor_mac.mm).
  setenv("TZ", zone.c_str(), 1);
  [NSNotificationCenter.defaultCenter
      postNotificationName:NSSystemTimeZoneDidChangeNotification
                    object:nil];
}

}  // namespace pane
