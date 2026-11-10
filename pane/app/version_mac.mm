// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/version.h"

#import <Foundation/Foundation.h>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"

namespace pane {

std::optional<std::string> BundledPaneVersion() {
  NSString* version = base::apple::ObjCCast<NSString>(
      [base::apple::OuterBundle() objectForInfoDictionaryKey:@"PaneVersion"]);
  if (!version.length) {
    return std::nullopt;
  }
  return base::SysNSStringToUTF8(version);
}

}  // namespace pane
