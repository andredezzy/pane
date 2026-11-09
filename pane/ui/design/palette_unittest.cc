// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

namespace pane {

namespace {

// Palette.swift's NSColor(rgb:alpha:), and NSColor(white:alpha:) at its two
// ends, written as the Swift writes them.
SkColor Swift(uint32_t rgb, float alpha = 1) {
  return SkColorSetARGB(static_cast<int>(alpha * 255 + 0.5f),
                        (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

SkColor White(float alpha) {
  return Swift(0xFFFFFF, alpha);
}

SkColor Black(float alpha) {
  return Swift(0x000000, alpha);
}

struct Pair {
  std::string name;
  ui::ColorId id;
  SkColor light;
  SkColor dark;
};

ui::ColorProviderKey KeyFor(ui::ColorProviderKey::ColorMode mode) {
  ui::ColorProviderKey key;
  key.color_mode = mode;
  return key;
}

}  // namespace

// Each color resolves to the Swift's pair: its light color under a light
// appearance, its dark one under a dark one, the ones made from another (a
// press, a hover) from that one's color in the same appearance, and scrims
// black in both.
TEST(PaletteTest, ResolvesTheSwiftsPairs) {
  const Pair pairs[] = {
      {"windowBackground", kPaneColorWindowBackground, Swift(0xFAFAFA),
       Swift(0x0A0A0C)},
      {"foreground", kPaneColorForeground, Swift(0x18181B), Swift(0xFAFAFA)},
      {"card", kPaneColorCard, Swift(0xFFFFFF), Swift(0x1C1C1F)},
      {"popover", kPaneColorPopover, Swift(0xFFFFFF), Swift(0x1C1C1F)},
      {"primary", kPaneColorPrimary, Swift(0x18181B), Swift(0xFAFAFA)},
      {"primaryForeground", kPaneColorPrimaryForeground, Swift(0xFAFAFA),
       Swift(0x18181B)},
      {"secondary", kPaneColorSecondary, Swift(0xE9E9EB), Swift(0x232326)},
      {"mutedForeground", kPaneColorMutedForeground, Swift(0x71717A),
       Swift(0x71717A)},
      {"accent", kPaneColorAccent, Swift(0xE9E9EB), Swift(0x232326)},
      {"accentForeground", kPaneColorAccentForeground, Swift(0x27272A),
       Swift(0xE4E4E7)},
      {"destructive", kPaneColorDestructive, Swift(0xDC2626), Swift(0xEF4444)},
      {"destructiveForeground", kPaneColorDestructiveForeground,
       Swift(0xFAFAFA), Swift(0xFAFAFA)},
      {"ring", kPaneColorRing, Swift(0xA1A1AA), Swift(0x52525B)},
      {"border", kPaneColorBorder, Black(0.08), White(0.06)},
      {"hairlineStrong", kPaneColorHairlineStrong, Black(0.08), White(0.1)},
      {"press", kPaneColorPress, Swift(0x18181B, 0.06), Swift(0xFAFAFA, 0.06)},
      {"pressOnStrong", kPaneColorPressOnStrong, Swift(0xFFFFFF, 0.1),
       Swift(0x1C1C1F, 0.1)},
      {"outlineHover", kPaneColorOutlineHover, Swift(0x18181B, 0.04),
       Swift(0xFAFAFA, 0.04)},
      {"rowSelected", kPaneColorRowSelected, Swift(0x18181B, 0.05),
       Swift(0xFAFAFA, 0.05)},
      {"popoverRing", kPaneColorPopoverRing, Swift(0x18181B, 0.1),
       Swift(0xFAFAFA, 0.1)},
      {"popupBorder", kPaneColorPopupBorder, Swift(0x18181B, 0.08),
       Swift(0xFAFAFA, 0.08)},
      {"pinned", kPaneColorPinned, Swift(0x3B82F6), Swift(0x60A5FA)},
      {"warning", kPaneColorWarning, Swift(0xEAB308), Swift(0xEAB308)},
      {"success", kPaneColorSuccess, Swift(0x10B981), Swift(0x10B981)},
      {"failure", kPaneColorFailure, Swift(0xEF4444), Swift(0xEF4444)},
      {"sheetCard", kPaneColorSheetCard, Swift(0xFAFAFA), Swift(0x151517)},
      {"sheetCardField", kPaneColorSheetCardField, Swift(0xFFFFFF),
       Swift(0x0A0A0C)},
      {"chosenTile", kPaneColorChosenTile, Swift(0xFFFFFF), White(0.06)},
      {"warningFill", kPaneColorWarningFill, Swift(0xEAB308, 0.1),
       Swift(0xEAB308, 0.12)},
      {"warningIcon", kPaneColorWarningIcon, Swift(0xA16207), Swift(0xEAB308)},
      {"appleLogo", kPaneColorAppleLogo, Swift(0x000000), Swift(0xFFFFFF)},
      {"tuxEdge", kPaneColorTuxEdge, White(0), White(0.6)},
      {"letterTile", kPaneColorLetterTile, Swift(0x757575), Swift(0x757575)},
      {"badgeRing", kPaneColorBadgeRing, Swift(0x444444), Swift(0x444444)},
      {"sheetScrim", kPaneColorSheetScrim, Black(0.4), Black(0.4)},
      {"modalScrim", kPaneColorModalScrim, Black(0.14), Black(0.45)},
      {"dialogShadow", kPaneColorDialogShadow, Black(0.16), Black(0.5)},
      {"installDialogShadow", kPaneColorInstallDialogShadow, Black(0.25),
       Black(0.5)},
      {"panelShadow", kPaneColorPanelShadow, Black(0.08), Black(0.25)},
      {"popoverShadow", kPaneColorPopoverShadow, Black(0.14), Black(0.4)},
      {"iconTile", kPaneColorIconTile, Swift(0xE9E9EB, 0.7), Swift(0x232326)},
      {"mutedLifted", kPaneColorMutedLifted, Swift(0x71717A), Swift(0xA1A1AA)},
      {"dialogMessage", kPaneColorDialogMessage, Swift(0x18181B, 0.72),
       Swift(0xFAFAFA, 0.68)},
      {"primaryKey", kPaneColorPrimaryKey, Swift(0xFAFAFA, 0.85),
       Swift(0x18181B, 0.7)},
      {"primaryKeyChip", kPaneColorPrimaryKeyChip, Swift(0xFAFAFA, 0.18),
       Swift(0x18181B, 0.12)},
      {"addressDivider", kPaneColorAddressDivider, Swift(0x858587),
       Swift(0x606068)},
      {"panelTint", kPaneColorPanelTint, Swift(0x18181B, 0.04),
       Swift(0xFAFAFA, 0.05)},
      {"Alert.panel", kPaneColorAlertPanel, Swift(0xEBEBEB, 0.95),
       Swift(0x2C2C2E, 0.95)},
      {"Alert.edge", kPaneColorAlertEdge, Black(0.12), White(0.14)},
      {"Alert.text", kPaneColorAlertText, Swift(0x1D1D1F), Swift(0xF5F5F7)},
      {"Alert.field", kPaneColorAlertField, Swift(0xFFFFFF), Swift(0x1C1C1E)},
      {"Alert.fieldEdge", kPaneColorAlertFieldEdge, Black(0.18), White(0.18)},
      {"Alert.placeholder", kPaneColorAlertPlaceholder, Swift(0x8E8E93),
       Swift(0x8E8E93)},
      {"Alert.button", kPaneColorAlertButton, Swift(0xDADADA), Swift(0x48484A)},
      {"Alert.defaultButton", kPaneColorAlertDefaultButton, Swift(0x3478F6),
       Swift(0x0A84FF)},
      {"Alert.shadow", kPaneColorAlertShadow, Black(0.25), Black(0.25)},
  };
  // Every id of Pane's is checked.
  EXPECT_EQ(std::size(pairs),
            static_cast<size_t>(kPaneColorsEnd - kPaneColorsStart));

  ui::ColorProvider light;
  AddPaneColors(&light, KeyFor(ui::ColorProviderKey::ColorMode::kLight));
  ui::ColorProvider dark;
  AddPaneColors(&dark, KeyFor(ui::ColorProviderKey::ColorMode::kDark));
  for (const Pair& pair : pairs) {
    SCOPED_TRACE(pair.name);
    EXPECT_EQ(light.GetColor(pair.id), pair.light);
    EXPECT_EQ(dark.GetColor(pair.id), pair.dark);
  }
}

// Pane's ids come after Chrome's, which the engine's own views resolve.
TEST(PaletteTest, IdsFollowChromes) {
  EXPECT_EQ(static_cast<ui::ColorId>(kPaneColorsStart),
            static_cast<ui::ColorId>(kChromeColorsEnd));
  EXPECT_GT(kPaneColorsEnd, kPaneColorsStart);
}

}  // namespace pane
