// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_TEXT_H_
#define PANE_UI_DESIGN_TEXT_H_

#include <memory>
#include <string>

#include "ui/color/color_variant.h"
#include "ui/gfx/font.h"

namespace gfx {
class FontList;
}

namespace views {
class Label;
}

namespace pane {

// SwiftUI's weights, as Chromium's Mac fonts map them (platform_font_mac.mm):
// SwiftUI's .thin is gfx's EXTRA_LIGHT (NSFontWeightThin), and gfx's THIN is
// .ultraLight.
namespace weight {
inline constexpr gfx::Font::Weight kThin = gfx::Font::Weight::EXTRA_LIGHT;
inline constexpr gfx::Font::Weight kLight = gfx::Font::Weight::LIGHT;
inline constexpr gfx::Font::Weight kRegular = gfx::Font::Weight::NORMAL;
inline constexpr gfx::Font::Weight kMedium = gfx::Font::Weight::MEDIUM;
inline constexpr gfx::Font::Weight kSemibold = gfx::Font::Weight::SEMIBOLD;
inline constexpr gfx::Font::Weight kBold = gfx::Font::Weight::BOLD;
}  // namespace weight

// A run of Pane's text, as the Swift app sets one: SwiftUI's
// .system(size:weight:) in a CSS line box (Labels.swift's lineBox: text-xs is
// a 16-point line, text-sm a 20, leading-none the size itself), the text
// centered in it.
struct TextStyle {
  int size = 13;
  gfx::Font::Weight weight = weight::kRegular;
  // The line box's height; 0 takes the font's own.
  int line_height = 0;
};

// The styles the Swift's controls share (Buttons.swift, Fields.swift,
// Labels.swift, Menus.swift). Tracking and tabular digits have no Views path
// (Chromium shapes with no OpenType features, render_text_harfbuzz.cc), so
// the section label's 0.05 em goes without, as its four uses and the five of
// monospacedDigit do until Paper decides them.
namespace text {
// The sidebar's and the toolbar's text: 12 points (ADR 0060).
inline constexpr TextStyle kSidebar{12, weight::kRegular, 16};
// A button's title: h-9's text-sm medium, h-8's text-xs, a popup's 13.
inline constexpr TextStyle kButton{14, weight::kMedium, 20};
inline constexpr TextStyle kButtonSmall{12, weight::kMedium, 16};
inline constexpr TextStyle kButtonPopup{13, weight::kMedium, 18};
// A field's text and placeholder: text-sm.
inline constexpr TextStyle kField{14, weight::kRegular, 20};
// A menu trigger's value: 13.
inline constexpr TextStyle kMenuValue{13, weight::kRegular, 18};
// The Extensions page's list label: 10 medium caps on a 15-point line.
inline constexpr TextStyle kSectionLabel{10, weight::kMedium, 15};
// A form's label, 11 medium on an 11-point line (Paper 03.8), and the profile
// sheet's, 12 on 16; a note beside either is regular.
inline constexpr TextStyle kFormLabel{11, weight::kMedium, 11};
inline constexpr TextStyle kFormLabelRegular{12, weight::kMedium, 16};
// A row's title, 13 on 18-point lines, and its description, 12 on 16
// (Settings' rows, the profile sheet's).
inline constexpr TextStyle kRowTitle{13, weight::kRegular, 18};
inline constexpr TextStyle kRowDescription{12, weight::kRegular, 16};
}  // namespace text

// The system font at `size` points and `weight`, as SwiftUI's .system: SF on
// the Mac, the platform's UI font elsewhere. Each is made once.
const gfx::FontList& SystemFont(int size, gfx::Font::Weight weight);

// Sets `label` in Pane's text: the style's font and line box, `color` (a
// ui::ColorVariant, so a Theme change resolves it again), taken as it is
// rather than shifted for contrast, and no subpixel rendering, as Pane's UI
// text is drawn without font smoothing (ADR 0060).
void StyleLabel(views::Label& label,
                const TextStyle& style,
                ui::ColorVariant color);
std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        const TextStyle& style,
                                        ui::ColorVariant color);

}  // namespace pane

#endif  // PANE_UI_DESIGN_TEXT_H_
