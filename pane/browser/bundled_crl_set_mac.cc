// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/bundled_crl_set.h"

#include <CoreFoundation/CoreFoundation.h>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_path.h"

namespace pane {

// The app bundle's own resource, as Foundation's main bundle finds it.
std::optional<base::FilePath> BundledCRLSet() {
  const base::apple::ScopedCFTypeRef<CFURLRef> url(CFBundleCopyResourceURL(
      CFBundleGetMainBundle(), CFSTR("crl-set"), nullptr, nullptr));
  if (!url) {
    return std::nullopt;
  }
  return base::apple::CFURLToFilePath(url.get());
}

}  // namespace pane
