// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

#include <CoreServices/CoreServices.h>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_path.h"
#include "base/mac/mac_util.h"

namespace pane {

void OpenUpdateFile(const base::FilePath& file) {
  const base::apple::ScopedCFTypeRef<CFURLRef> url =
      base::apple::FilePathToCFURL(file);
  if (url) {
    LSOpenCFURLRef(url.get(), nullptr);
  }
}

bool ReleaseFromQuarantine(const base::FilePath& file) {
  return base::mac::RemoveQuarantineAttribute(file);
}

}  // namespace pane
