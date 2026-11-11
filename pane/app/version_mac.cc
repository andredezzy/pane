// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/version.h"

#include <CoreFoundation/CoreFoundation.h>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"

namespace pane {

std::optional<std::string> BundledPaneVersion() {
  const base::apple::ScopedCFTypeRef<CFBundleRef> bundle(CFBundleCreate(
      kCFAllocatorDefault,
      base::apple::FilePathToCFURL(base::apple::OuterBundlePath()).get()));
  CFStringRef version = bundle ? base::apple::CFCast<CFStringRef>(
                                     CFBundleGetValueForInfoDictionaryKey(
                                         bundle.get(), CFSTR("PaneVersion")))
                               : nullptr;
  if (!version || !CFStringGetLength(version)) {
    return std::nullopt;
  }
  return base::SysCFStringRefToUTF8(version);
}

}  // namespace pane
