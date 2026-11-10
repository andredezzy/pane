// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/theme.h"

#include <string>

#include "pane/model/test_hooks.h"

namespace pane {

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
  // A scenario's stand-in for the system's appearance.
  const std::optional<std::string> system =
      model::TestHook("PANE_SYSTEM_APPEARANCE");
  ui::NativeTheme::SetPreferredColorSchemeOverride(
      PreferredColorSchemeFor(theme, system));
}

}  // namespace pane
