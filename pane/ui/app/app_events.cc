// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#include <optional>
#include <string>

#include "pane/model/test_hooks.h"

namespace pane {

bool MayTakeFront() {
  return !model::TestHook("PANE_AUTOMATION_DIR") ||
         model::TestHook("PANE_AUTOMATION_FOREGROUND") ==
             std::optional<std::string>("1");
}

}  // namespace pane
