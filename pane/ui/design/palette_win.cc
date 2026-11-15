// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#include <windows.h>

#include <initializer_list>

#include "skia/ext/skia_utils_win.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace pane {

// Windows keeps one selection color, its highlight, in either appearance and
// whether the window has the keys or not.
SkColor SystemTextSelection(bool dark, bool key_window) {
  return skia::COLORREFToSkColor(GetSysColor(COLOR_HIGHLIGHT));
}

// And draws the text under it in HighlightText, white by default.
std::optional<SkColor> SystemSelectedTextColor() {
  return skia::COLORREFToSkColor(GetSysColor(COLOR_HIGHLIGHTTEXT));
}

// Windows' contrast themes (Paper 11.10, chosen): while one is on, each of
// Pane's colors takes one of its roles, as Chrome's pages and Views' menus
// do already. Backgrounds take Canvas (the theme's Window), text CanvasText
// (WindowText), what is disabled GrayText, the selection Highlight with
// HighlightText, Pane's own buttons ButtonFace and ButtonText, its strong
// ones Highlight. A fill that told a surface apart (the panel on the frame,
// a row's hover, a button's grey) gives way to a 1-point CanvasText edge
// (kPaneColorForcedEdge, which controls stroke there), shadows and veils go,
// and the focus ring takes Highlight. A profile's color and a status's dot
// keep theirs, as a page's icons do.
void AddForcedColors(ui::ColorMixer& mixer) {
  const auto take = [&](std::initializer_list<ui::ColorId> ids,
                        ui::ColorId role) {
    for (ui::ColorId id : ids) {
      mixer[id] = {role};
    }
  };
  const auto clear = [&](std::initializer_list<ui::ColorId> ids) {
    for (ui::ColorId id : ids) {
      mixer[id] = {SK_ColorTRANSPARENT};
    }
  };
  // Canvas: every surface, one ground.
  take({kPaneColorWindowBackground, kPaneColorCard, kPaneColorPopover,
        kPaneColorSheetCard, kPaneColorSheetCardField, kPaneColorChosenTile,
        kPaneColorIconTile, kPaneColorPanelTint, kPaneColorAlertPanel,
        kPaneColorAlertField, kPaneColorActionPopup, kPaneColorHint,
        kPaneColorWarningFill},
       ui::kColorNativeWindow);
  // CanvasText: text, icons, and every edge.
  take({kPaneColorForeground,       kPaneColorMutedForeground,
        kPaneColorAccentForeground, kPaneColorMutedLifted,
        kPaneColorDialogMessage,    kPaneColorHeadingMessage,
        kPaneColorAlertText,        kPaneColorAlertPlaceholder,
        kPaneColorHintText,         kPaneColorPinTitle,
        kPaneColorPinQuiet,         kPaneColorPinHint,
        kPaneColorPinHintLit,       kPaneColorWarningIcon,
        kPaneColorAppleLogo,        kPaneColorBorder,
        kPaneColorHairlineStrong,   kPaneColorPopoverRing,
        kPaneColorPopupBorder,      kPaneColorAlertEdge,
        kPaneColorAlertFieldEdge,   kPaneColorActionPopupEdge,
        kPaneColorFindBarRing,      kPaneColorAddressDivider,
        kPaneColorSidebarRule,      kPaneColorBadgeRing,
        kPaneColorForcedEdge},
       ui::kColorNativeWindowText);
  // A sleeping tab: GrayText, in its place of a dimmed row.
  mixer[kPaneColorForcedGrayText] = {ui::kColorNativeGrayText};
  // What a fill in Highlight is filled with, which needs no edge.
  mixer[kPaneColorForcedHighlight] = {ui::kColorNativeHighlight};
  // Pane's own buttons: ButtonFace under ButtonText.
  take({kPaneColorSecondary, kPaneColorAlertButton}, ui::kColorNativeBtnFace);
  // The strong ones, and the selection: Highlight under HighlightText.
  take({kPaneColorPrimary, kPaneColorAlertDefaultButton, kPaneColorRowSelected,
        kPaneColorTextSelection, kPaneColorTextSelectionInactive,
        kPaneColorRing, kPaneColorKeyboardRing},
       ui::kColorNativeHighlight);
  take({kPaneColorPrimaryForeground, kPaneColorDestructiveForeground,
        kPaneColorAlertDefaultText, kPaneColorRowSelectedForeground,
        kPaneColorPrimaryKey, kPaneColorDestructiveKey,
        kPaneColorTextSelectionForeground},
       ui::kColorNativeHighlightText);
  // Exit Pane?'s Exit, as its other strong buttons, and the lock's lines:
  // no red.
  mixer[kPaneColorDestructive] = {ui::kColorNativeHighlight};
  // A key's chip on a strong button: an edge, not a fill.
  clear({kPaneColorPrimaryKeyChip, kPaneColorDestructiveKeyChip});
  // Fills that told a surface apart, hovers and presses: the edge instead.
  clear({kPaneColorAccent, kPaneColorPress, kPaneColorPressOnStrong,
         kPaneColorOutlineHover, kPaneColorAddressHover,
         kPaneColorMenuButtonHover, kPaneColorUpdatePill, kPaneColorUpdateTrack,
         kPaneColorAlertPress});
  // Shadows and veils go.
  clear({kPaneColorSheetScrim, kPaneColorModalScrim, kPaneColorDialogShadow,
         kPaneColorInstallDialogShadow, kPaneColorPanelShadow,
         kPaneColorPopoverShadow, kPaneColorAlertShadow,
         kPaneColorFindBarShadow});
  // Views' menus (11.10.4): the edge a contrast theme strokes them with
  // (menu_scroll_view_container.cc's hunk, patch 0004) in WindowText, where
  // Chromium's own colors give it ButtonFace, the menu's fill. Their shadows
  // take the Window's color, so none shows on the theme's ground: a menu's
  // shadow keeps the base's color at an alpha of its own, so no color clears
  // it. That base, clear, also gives BubbleBorder's cached paint, which it
  // keys by the base alone, a key no appearance shares: a menu after a switch
  // never takes another theme's edge.
  mixer[ui::kColorBubbleBorder] = {ui::kColorNativeWindowText};
  mixer[ui::kColorShadowBase] = ui::SetAlpha(ui::kColorNativeWindow, 0);
}

}  // namespace pane
