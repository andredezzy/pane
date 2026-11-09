// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/theme.h"

#include <string>

#include "base/command_line.h"
#include "base/environment.h"

namespace pane {

namespace {

// A scenario's stand-in for the system's appearance: on the mock keychain
// alone, as the Swift app's TestHooks read it.
std::optional<std::string> SystemAppearanceForTest() {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch("use-mock-keychain")) {
    return std::nullopt;
  }
  return base::Environment::Create()->GetVar("PANE_SYSTEM_APPEARANCE");
}

}  // namespace

std::optional<ui::NativeTheme::PreferredColorScheme> PreferredColorSchemeFor(
    model::Theme theme,
    std::optional<std::string_view> system_appearance) {
  using Scheme = ui::NativeTheme::PreferredColorScheme;
  switch (theme) {
    case model::Theme::kLight:
      return Scheme::kLight;
    case model::Theme::kDark:
      return Scheme::kDark;
    case model::Theme::kSystem:
      if (system_appearance == "light") {
        return Scheme::kLight;
      }
      if (system_appearance == "dark") {
        return Scheme::kDark;
      }
      return std::nullopt;
  }
}

void ApplyTheme(model::Theme theme) {
  const std::optional<std::string> system = SystemAppearanceForTest();
  ui::NativeTheme::SetPreferredColorSchemeOverride(
      PreferredColorSchemeFor(theme, system));
}

}  // namespace pane
