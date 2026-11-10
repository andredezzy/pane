// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/mac/mac_util.h"

namespace pane {

void OpenUpdateFile(const base::FilePath& file) {
  [NSWorkspace.sharedWorkspace openURL:base::apple::FilePathToNSURL(file)];
}

bool ReleaseFromQuarantine(const base::FilePath& file) {
  return base::mac::RemoveQuarantineAttribute(file);
}

}  // namespace pane
