// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"

namespace pane {

namespace {

// Aquatic's Window and WindowText.
constexpr SkColor kWindow = SkColorSetRGB(0x20, 0x20, 0x20);
constexpr SkColor kWindowText = SK_ColorWHITE;

// A provider with a contrast theme's roles as Chromium's mixers leave them
// (a bubble's stroke in ButtonFace, the dark appearance's shadow base), then
// Pane's colors, under that theme's forced colors or with none.
void AddTheme(ui::ColorProvider& provider, bool forced) {
  ui::ColorMixer& chromium = provider.AddMixer();
  chromium[ui::kColorNativeWindow] = {kWindow};
  chromium[ui::kColorNativeWindowText] = {kWindowText};
  chromium[ui::kColorNativeBtnFace] = {kWindow};
  chromium[ui::kColorBubbleBorder] = {ui::kColorNativeBtnFace};
  chromium[ui::kColorShadowBase] = {SK_ColorBLACK};
  ui::ColorProviderKey key;
  key.color_mode = ui::ColorProviderKey::ColorMode::kDark;
  key.forced_colors = forced ? ui::ColorProviderKey::ForcedColors::kSystem
                             : ui::ColorProviderKey::ForcedColors::kNone;
  AddPaneColors(&provider, key);
}

}  // namespace

// Under a contrast theme Views' menus take their edge in WindowText (Paper
// 11.10.4), where Chromium's colors give it ButtonFace, the menu's own fill;
// their shadow the Window's color, clear, a base no appearance shares, so
// BubbleBorder's paint, cached by that base, is the theme's own. Without one,
// Chromium's colors stand.
TEST(PaletteWinTest, AContrastThemesMenusTakeItsTextColorsEdge) {
  ui::ColorProvider contrast;
  AddTheme(contrast, /*forced=*/true);
  EXPECT_EQ(contrast.GetColor(ui::kColorBubbleBorder), kWindowText);
  EXPECT_EQ(contrast.GetColor(ui::kColorShadowBase), SkColorSetA(kWindow, 0));

  ui::ColorProvider plain;
  AddTheme(plain, /*forced=*/false);
  EXPECT_EQ(plain.GetColor(ui::kColorBubbleBorder), kWindow);
  EXPECT_EQ(plain.GetColor(ui::kColorShadowBase), SK_ColorBLACK);
}

}  // namespace pane
