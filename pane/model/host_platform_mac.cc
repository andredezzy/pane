// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/host_platform.h"

namespace pane::model {

Platform HostPlatform() {
  return Platform::kMacOS;
}

std::string_view HostDeviceName() {
  return "Mac";
}

}  // namespace pane::model
