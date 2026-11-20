// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/host_platform.h"

#include "base/strings/strcat.h"

namespace pane::model {

Platform HostPlatform() {
  return Platform::kWindows;
}

std::string_view HostDeviceName() {
  return "PC";
}

std::optional<std::string> HostDataPromise(bool chip_guards_pin) {
  return base::StrCat(
      {"Without a PIN, any app you run can open Pane’s data",
       chip_guards_pin ? "; with one, the PC’s security chip guards it." : ".",
       " Files’ sizes and times and extensions’ code still show on "
       "disk, and while Pane is unlocked, apps can read its memory."});
}

}  // namespace pane::model
