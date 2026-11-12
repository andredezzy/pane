// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

#include <CoreServices/CoreServices.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_path.h"
#include "base/mac/mac_util.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"

namespace pane {

void OpenUpdateFile(const base::FilePath& file) {
  const base::apple::ScopedCFTypeRef<CFURLRef> url =
      base::apple::FilePathToCFURL(file);
  if (url) {
    LSOpenCFURLRef(url.get(), nullptr);
  }
}

void OpenUpdateFileAfterExit(const base::FilePath& file) {
  OpenAfterProcessExits(getpid(), file, base::FilePath("/usr/bin/open"));
}

bool OpenAfterProcessExits(int pid,
                           const base::FilePath& file,
                           const base::FilePath& opener) {
  // $1 the process, $2 what opens, $3 the file. `open` is no app, so what it
  // launches is no app's helper.
  static constexpr char kScript[] =
      "pid=$1; while kill -0 \"$pid\" 2>/dev/null; do sleep 0.05; done; "
      "exec \"$2\" \"$3\"";
  const std::vector<std::string> argv = {"/bin/sh",
                                         "-c",
                                         kScript,
                                         "pane-update-open",
                                         base::NumberToString(pid),
                                         opener.value(),
                                         file.value()};
  // Apart from Pane's process group, as the installer's shell is: a terminal
  // that closes takes no part of it with it.
  base::LaunchOptions options;
  options.new_process_group = true;
  return base::LaunchProcess(argv, options).IsValid();
}

bool ReleaseFromQuarantine(const base::FilePath& file) {
  return base::mac::RemoveQuarantineAttribute(file);
}

}  // namespace pane
