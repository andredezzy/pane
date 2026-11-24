// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_PALETTE_H_
#define PANE_UI_DESIGN_PALETTE_H_

#include <optional>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "pane/model/profile.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"

namespace ui {
class ColorMixer;
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace pane {

// The design's colors (the Swift app's Palette, and its Elevation's shadows):
// the Electron app's tokens, which Paper documents too, each a light and dark
// pair resolved by the color provider of the window's appearance (Theme). A
// view takes one as a ui::ColorVariant, so a Theme change resolves it again
// everywhere at once. Scrims stay black in both, and colors on fixed-color
// surfaces (a status's dot, a letter tile) are one color in both (DESIGN.md ›
// Theme). After Chrome's ids, which the engine's own views use.
enum PaneColorIds : ui::ColorId {
  kPaneColorsStart = kChromeColorsEnd,

  // The window's own fill.
  kPaneColorWindowBackground = kPaneColorsStart,
  kPaneColorForeground,
  kPaneColorCard,
  kPaneColorPopover,
  kPaneColorPrimary,
  kPaneColorPrimaryForeground,
  kPaneColorSecondary,
  kPaneColorMutedForeground,
  kPaneColorAccent,
  kPaneColorAccentForeground,
  kPaneColorDestructive,
  kPaneColorDestructiveForeground,
  // A focused field's ring.
  kPaneColorRing,
  // Borders and input outlines: black 8% on light, white 6% on dark.
  kPaneColorBorder,
  // A border a little stronger on dark (white 10%): the dialogs' Cancel.
  kPaneColorHairlineStrong,
  // A press over a button's fill: the foreground at 6%, and over a strong
  // fill (primary, destructive), the card at 10%.
  kPaneColorPress,
  kPaneColorPressOnStrong,
  // An outline control's hover (ADR 0063): the foreground at 4%.
  kPaneColorOutlineHover,
  // A selected sidebar row's fill: the foreground at 5%.
  kPaneColorRowSelected,
  // The rule between the sidebar footer's New and Settings: the foreground
  // at 4%.
  kPaneColorSidebarRule,
  // A popover's ring (the switcher, the extensions menus): the foreground
  // at 10%; a popup's Cancel border, at 8%.
  kPaneColorPopoverRing,
  kPaneColorPopupBorder,
  // Aside's pinned blue, for an extension's pin.
  kPaneColorPinned,
  // Status colors, the same in both: a proxy's dot (testing, connected,
  // failed) and the update's dot and bar.
  kPaneColorWarning,
  kPaneColorSuccess,
  kPaneColorFailure,
  // The profile sheet: a card on it, a field on the card, the chosen
  // platform's tile, a warning's fill and icon.
  kPaneColorSheetCard,
  kPaneColorSheetCardField,
  kPaneColorChosenTile,
  kPaneColorWarningFill,
  kPaneColorWarningIcon,
  // Apple's logo in its one color on each ground, and Tux's edge on dark.
  kPaneColorAppleLogo,
  kPaneColorTuxEdge,
  // An extension without an icon: its letter's tile, a badge's ring.
  kPaneColorLetterTile,
  kPaneColorBadgeRing,
  // The profile sheet's scrim; Quit Pane?'s and the install dialog's, a
  // lighter veil, darker on dark.
  kPaneColorSheetScrim,
  kPaneColorModalScrim,
  // Shadows: Quit Pane?'s, the add-extension dialog's (Dia's), the content
  // panel's (shadow-panel), and a popover's.
  kPaneColorDialogShadow,
  kPaneColorInstallDialogShadow,
  kPaneColorPanelShadow,
  kPaneColorPopoverShadow,
  // Quit Pane?'s icon tile.
  kPaneColorIconTile,
  // Muted, lighter on dark: the key on Cancel's chip, a platform's name, an
  // embedded menu's chevrons.
  kPaneColorMutedLifted,
  // A dialog's message under its title: Quit Pane?'s, and the foreground at
  // 70% (DialogHeading's own, the App icon picker's).
  kPaneColorDialogMessage,
  kPaneColorHeadingMessage,
  // The key on a primary button's chip, and the chip; on a destructive
  // one's, white at 85% on white at 18%, the same in both.
  kPaneColorPrimaryKey,
  kPaneColorPrimaryKeyChip,
  kPaneColorDestructiveKey,
  kPaneColorDestructiveKeyChip,
  // The rule between the address's host and path.
  kPaneColorAddressDivider,
  // The address field under the pointer: the foreground at 3%.
  kPaneColorAddressHover,
  // The address's load bar, the same in both: blue-500 to blue-400 across
  // what it has filled, and Chrome's shimmer over it, blue-500 with 40%
  // white (ADR 0081).
  kPaneColorLoadBarStart,
  kPaneColorLoadBarEnd,
  kPaneColorLoadShimmer,
  // The update pill's fill, the foreground at 5%, and its bar's track, at
  // 10%.
  kPaneColorUpdatePill,
  kPaneColorUpdateTrack,
  // Selected text in a field, as the platform's own text views draw it: the
  // system's highlight while the window has the keys, and its unemphasized
  // one while it doesn't (the Mac's selectedTextBackgroundColor and
  // unemphasizedSelectedTextBackgroundColor, which follow the user's
  // Highlight color).
  kPaneColorTextSelection,
  kPaneColorTextSelectionInactive,
  // A panel on a dialog's card.
  kPaneColorPanelTint,
  // A page's dialog, drawn as macOS draws an alert (Paper 04.15 to 04.23).
  kPaneColorAlertPanel,
  kPaneColorAlertEdge,
  kPaneColorAlertText,
  kPaneColorAlertField,
  kPaneColorAlertFieldEdge,
  kPaneColorAlertPlaceholder,
  kPaneColorAlertButton,
  kPaneColorAlertDefaultButton,
  kPaneColorAlertShadow,
  // The default button's title, white in both, and a button's press, black
  // at 12% over its fill.
  kPaneColorAlertDefaultText,
  kPaneColorAlertPress,
  // The find bar's shadow, black at 25%, 40% on dark, and its hairline ring,
  // which only dark draws, white at 10% (ADR 0067).
  kPaneColorFindBarShadow,
  kPaneColorFindBarRing,
  // The full screen's Esc hint: white on black at 75%, the same in both
  // (ADR 0025).
  kPaneColorHint,
  kPaneColorHintText,
  // An action's popup, as NSPopover drew it and Paper 04.12 draws it: its
  // material, white or #2C2C2E at 96%, and its hairline, black at 12% or
  // white at 14% (ADR 0031); the extension's page sits on white in both.
  kPaneColorActionPopup,
  kPaneColorActionPopupEdge,
  kPaneColorActionPopupCanvas,
  // A pinned action's badge, where the extension set no color of its own:
  // the system's red under white text, as Chrome draws it (ADR 0054).
  kPaneColorBadgeDefault,
  kPaneColorBadgeDefaultText,
  // The extensions menu's pin and more under the pointer: the foreground at
  // 8%.
  kPaneColorMenuButtonHover,
  // The PIN screen's words (Paper 07): a step's title, the foreground at 50%;
  // a quiet line (Enter at least 4 digits, the attempts left) and the back
  // arrow at rest, at 30%; Continue at rest and the back arrow lit, at 60%;
  // Continue lit, at 80% (Paper 03.6).
  kPaneColorPinTitle,
  kPaneColorPinQuiet,
  kPaneColorPinHint,
  kPaneColorPinHintLit,
  // A selected sidebar row's text: the accent's foreground, the selection's
  // text under a contrast theme.
  kPaneColorRowSelectedForeground,
  // The edge a control draws where its fill told it apart, under a contrast
  // theme alone (Paper 11.10: edges, not fills); clear otherwise.
  kPaneColorForcedEdge,
  // What a sleeping tab's row shows in, in its place of a dimmed one, under
  // a contrast theme alone (Paper 11.10: its GrayText); clear otherwise.
  kPaneColorForcedGrayText,
  // The theme's Highlight, under a contrast theme alone: a fill in it tells
  // its control apart by itself, and takes no edge (Paper 11.10.2 to
  // 11.10.5: a selected row, a default button, Create); clear otherwise.
  kPaneColorForcedHighlight,
  // The ring of a control the keys reach where the system draws it in the
  // text's color (Windows, Paper 11.17 B; ControlFocusRingStyle): the
  // foreground, and Highlight under a contrast theme.
  kPaneColorKeyboardRing,
  // Selected text where the system draws it in a color of its own over its
  // highlight (Windows' HighlightText, Paper 11.18.1;
  // SystemSelectedTextColor); unused where a field keeps its own.
  kPaneColorTextSelectionForeground,

  kPaneColorsEnd,
};

// An alpha of 0 to 1 at 8 bits, as AppKit draws a color's.
constexpr SkAlpha AlphaOf(float alpha) {
  return static_cast<SkAlpha>(alpha * 255 + 0.5f);
}

// A profile's swatch, the same in both appearances (DESIGN.md › Theme).
SkColor ProfileColorOf(model::ProfileColor color);

// The platform's selected-text fill in its light or dark appearance, while
// the window has the keys or doesn't.
SkColor SystemTextSelection(bool dark, bool key_window);

// The color the platform draws selected text in over that fill, where it
// has one of its own: Windows' HighlightText, white by default, as its own
// fields draw it (Paper 11.18.1: 4.5:1 where the field's own text was 3.9:1).
// None on the Mac, where a field keeps its own color, as an NSTextField's.
std::optional<SkColor> SystemSelectedTextColor();

// Pane's colors for a provider of `key`'s appearance: added once at startup,
// after Chrome's mixers (AddPaneColorMixer).
void AddPaneColors(ui::ColorProvider* provider,
                   const ui::ColorProviderKey& key);

// Pane's colors under the system's forced colors, which only Windows'
// contrast themes bring (Paper 11.10): each takes one of the theme's roles,
// a fill that told surfaces apart giving way to an edge, and shadows and
// veils going (palette_win.cc). The Mac has none: nothing changes there.
void AddForcedColors(ui::ColorMixer& mixer);

// Adds Pane's colors to every color provider made from now on.
void AddPaneColorMixer();

}  // namespace pane

#endif  // PANE_UI_DESIGN_PALETTE_H_
