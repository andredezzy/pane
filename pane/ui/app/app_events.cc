// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#include <memory>
#include <optional>
#include <string>

#include "base/environment.h"

namespace pane {

bool MayTakeFront() {
  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  return !environment->GetVar("PANE_AUTOMATION_DIR").has_value() ||
         environment->GetVar("PANE_AUTOMATION_FOREGROUND") ==
             std::optional<std::string>("1");
}

}  // namespace pane
