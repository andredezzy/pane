// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/theme.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace pane {

using Scheme = ui::NativeTheme::PreferredColorScheme;

// Light and dark set the app's scheme whatever the system's; System leaves it
// to the system, or to a test copy's stand-in for it.
TEST(ThemeTest, TheThemeSetsTheAppsScheme) {
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kLight, std::nullopt),
            Scheme::kLight);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kLight, "dark"),
            Scheme::kLight);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kDark, std::nullopt),
            Scheme::kDark);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kDark, "light"),
            Scheme::kDark);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, std::nullopt),
            std::nullopt);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, "light"),
            Scheme::kLight);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, "dark"),
            Scheme::kDark);
  // A value no scenario writes is the system's.
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, "sepia"),
            std::nullopt);
}

// Applying a Theme sets Chromium's one app-level knob, which every native
// theme, and so every widget's colors and every page's scheme, follows.
TEST(ThemeTest, ApplyingSetsTheOverride) {
  ApplyTheme(model::Theme::kDark);
  EXPECT_EQ(ui::NativeTheme::GetPreferredColorSchemeOverride(), Scheme::kDark);
  ApplyTheme(model::Theme::kLight);
  EXPECT_EQ(ui::NativeTheme::GetPreferredColorSchemeOverride(), Scheme::kLight);
  // No test copy's stand-in in a unit test: the system's.
  ApplyTheme(model::Theme::kSystem);
  EXPECT_EQ(ui::NativeTheme::GetPreferredColorSchemeOverride(), std::nullopt);
}

}  // namespace pane
