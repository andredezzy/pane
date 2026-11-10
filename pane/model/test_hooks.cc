// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/test_hooks.h"

#include <algorithm>
#include <memory>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/strings/string_number_conversions.h"

namespace pane::model {

std::optional<std::string> TestHook(base::cstring_view name) {
  if (!base::CommandLine::InitializedForCurrentProcess() ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch("use-mock-keychain")) {
    return std::nullopt;
  }
  std::optional<std::string> value = base::Environment::Create()->GetVar(name);
  if (!value || value->empty()) {
    return std::nullopt;
  }
  return value;
}

double TestTimeScale() {
  double scale = 1;
  const std::optional<std::string> value = TestHook("PANE_TIME_SCALE");
  if (!value || !base::StringToDouble(*value, &scale)) {
    return 1;
  }
  return std::max(scale, 1.0);
}

}  // namespace pane::model
