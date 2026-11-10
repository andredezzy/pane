// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#include <utility>

#include "base/functional/bind.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace pane {

namespace {

constexpr SkColor Rgb(uint32_t rgb) {
  return SkColorSetRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

constexpr SkColor Rgba(uint32_t rgb, float alpha) {
  return SkColorSetA(Rgb(rgb), AlphaOf(alpha));
}

// AppKit's NSColor(white:alpha:) at 0 and 1.
constexpr SkColor Black(float alpha) {
  return SkColorSetA(SK_ColorBLACK, AlphaOf(alpha));
}

constexpr SkColor White(float alpha) {
  return SkColorSetA(SK_ColorWHITE, AlphaOf(alpha));
}

}  // namespace

void AddPaneColors(ui::ColorProvider* provider,
                   const ui::ColorProviderKey& key) {
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;
  ui::ColorMixer& mixer = provider->AddMixer();
  // A pair: its light color, then its dark one.
  const auto set = [&](ui::ColorId id, SkColor light, SkColor dark_color) {
    mixer[id] = {dark ? dark_color : light};
  };

  set(kPaneColorWindowBackground, Rgb(0xFAFAFA), Rgb(0x0A0A0C));
  set(kPaneColorForeground, Rgb(0x18181B), Rgb(0xFAFAFA));
  set(kPaneColorCard, Rgb(0xFFFFFF), Rgb(0x1C1C1F));
  set(kPaneColorPopover, Rgb(0xFFFFFF), Rgb(0x1C1C1F));
  set(kPaneColorPrimary, Rgb(0x18181B), Rgb(0xFAFAFA));
  set(kPaneColorPrimaryForeground, Rgb(0xFAFAFA), Rgb(0x18181B));
  set(kPaneColorSecondary, Rgb(0xE9E9EB), Rgb(0x232326));
  set(kPaneColorMutedForeground, Rgb(0x71717A), Rgb(0x71717A));
  set(kPaneColorAccent, Rgb(0xE9E9EB), Rgb(0x232326));
  set(kPaneColorAccentForeground, Rgb(0x27272A), Rgb(0xE4E4E7));
  set(kPaneColorDestructive, Rgb(0xDC2626), Rgb(0xEF4444));
  set(kPaneColorDestructiveForeground, Rgb(0xFAFAFA), Rgb(0xFAFAFA));
  set(kPaneColorRing, Rgb(0xA1A1AA), Rgb(0x52525B));
  set(kPaneColorBorder, Black(0.08), White(0.06));
  set(kPaneColorHairlineStrong, Black(0.08), White(0.1));
  mixer[kPaneColorPress] = ui::SetAlpha(kPaneColorForeground, AlphaOf(0.06));
  mixer[kPaneColorPressOnStrong] = ui::SetAlpha(kPaneColorCard, AlphaOf(0.1));
  mixer[kPaneColorOutlineHover] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.04));
  mixer[kPaneColorRowSelected] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.05));
  mixer[kPaneColorSidebarRule] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.04));
  mixer[kPaneColorPopoverRing] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.1));
  mixer[kPaneColorPopupBorder] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.08));
  set(kPaneColorPinned, Rgb(0x3B82F6), Rgb(0x60A5FA));
  set(kPaneColorWarning, Rgb(0xEAB308), Rgb(0xEAB308));
  set(kPaneColorSuccess, Rgb(0x10B981), Rgb(0x10B981));
  set(kPaneColorFailure, Rgb(0xEF4444), Rgb(0xEF4444));
  set(kPaneColorSheetCard, Rgb(0xFAFAFA), Rgb(0x151517));
  set(kPaneColorSheetCardField, Rgb(0xFFFFFF), Rgb(0x0A0A0C));
  set(kPaneColorChosenTile, Rgb(0xFFFFFF), White(0.06));
  set(kPaneColorWarningFill, Rgba(0xEAB308, 0.1), Rgba(0xEAB308, 0.12));
  set(kPaneColorWarningIcon, Rgb(0xA16207), Rgb(0xEAB308));
  set(kPaneColorAppleLogo, Rgb(0x000000), Rgb(0xFFFFFF));
  set(kPaneColorTuxEdge, White(0), White(0.6));
  set(kPaneColorLetterTile, Rgb(0x757575), Rgb(0x757575));
  set(kPaneColorBadgeRing, Rgb(0x444444), Rgb(0x444444));
  set(kPaneColorSheetScrim, Black(0.4), Black(0.4));
  set(kPaneColorModalScrim, Black(0.14), Black(0.45));
  set(kPaneColorDialogShadow, Black(0.16), Black(0.5));
  set(kPaneColorInstallDialogShadow, Black(0.25), Black(0.5));
  set(kPaneColorPanelShadow, Black(0.08), Black(0.25));
  set(kPaneColorPopoverShadow, Black(0.14), Black(0.4));
  set(kPaneColorIconTile, Rgba(0xE9E9EB, 0.7), Rgb(0x232326));
  set(kPaneColorMutedLifted, Rgb(0x71717A), Rgb(0xA1A1AA));
  set(kPaneColorDialogMessage, Rgba(0x18181B, 0.72), Rgba(0xFAFAFA, 0.68));
  set(kPaneColorPrimaryKey, Rgba(0xFAFAFA, 0.85), Rgba(0x18181B, 0.7));
  set(kPaneColorPrimaryKeyChip, Rgba(0xFAFAFA, 0.18), Rgba(0x18181B, 0.12));
  set(kPaneColorAddressDivider, Rgb(0x858587), Rgb(0x606068));
  mixer[kPaneColorAddressHover] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.03));
  set(kPaneColorLoadBarStart, Rgb(0x3B82F6), Rgb(0x3B82F6));
  set(kPaneColorLoadBarEnd, Rgb(0x60A5FA), Rgb(0x60A5FA));
  // NSColor's blended(withFraction: 0.4, of: .white), rounded.
  set(kPaneColorLoadShimmer, Rgb(0x89B4FA), Rgb(0x89B4FA));
  mixer[kPaneColorUpdatePill] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.05));
  mixer[kPaneColorUpdateTrack] =
      ui::SetAlpha(kPaneColorForeground, AlphaOf(0.1));
  set(kPaneColorTextSelection, SystemTextSelection(false, true),
      SystemTextSelection(true, true));
  set(kPaneColorTextSelectionInactive, SystemTextSelection(false, false),
      SystemTextSelection(true, false));
  // Every field's selection, as AppKit's text views draw it: the system's
  // highlight in the appearance shown, where Chromium's own mixers end on
  // Material's tonal container in dark.
  mixer[ui::kColorTextfieldSelectionBackground] = {kPaneColorTextSelection};
  // NSColor(white: 0.98) on dark.
  set(kPaneColorPanelTint, Rgba(0x18181B, 0.04), Rgba(0xFAFAFA, 0.05));
  set(kPaneColorAlertPanel, Rgba(0xEBEBEB, 0.95), Rgba(0x2C2C2E, 0.95));
  set(kPaneColorAlertEdge, Black(0.12), White(0.14));
  set(kPaneColorAlertText, Rgb(0x1D1D1F), Rgb(0xF5F5F7));
  set(kPaneColorAlertField, Rgb(0xFFFFFF), Rgb(0x1C1C1E));
  set(kPaneColorAlertFieldEdge, Black(0.18), White(0.18));
  set(kPaneColorAlertPlaceholder, Rgb(0x8E8E93), Rgb(0x8E8E93));
  set(kPaneColorAlertButton, Rgb(0xDADADA), Rgb(0x48484A));
  set(kPaneColorAlertDefaultButton, Rgb(0x3478F6), Rgb(0x0A84FF));
  set(kPaneColorAlertShadow, Black(0.25), Black(0.25));
}

void AddPaneColorMixer() {
  static bool added = false;
  if (std::exchange(added, true)) {
    return;
  }
  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddPaneColors));
}

}  // namespace pane
