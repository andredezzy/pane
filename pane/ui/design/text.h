// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_TEXT_H_
#define PANE_UI_DESIGN_TEXT_H_

#include <memory>
#include <string>
#include <vector>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/font.h"
#include "ui/views/view.h"

namespace gfx {
class FontList;
class RenderText;
}  // namespace gfx

namespace views {
class Label;
}

namespace pane {

// The weights Pane's text takes, as Chromium's Mac fonts map them
// (platform_font_mac.mm): kThin, Paper's extralight (200, the PIN's keys), is
// gfx's EXTRA_LIGHT (NSFontWeightThin), and gfx's THIN is the system's
// UltraLight.
namespace weight {
inline constexpr gfx::Font::Weight kThin = gfx::Font::Weight::EXTRA_LIGHT;
inline constexpr gfx::Font::Weight kLight = gfx::Font::Weight::LIGHT;
inline constexpr gfx::Font::Weight kRegular = gfx::Font::Weight::NORMAL;
inline constexpr gfx::Font::Weight kMedium = gfx::Font::Weight::MEDIUM;
inline constexpr gfx::Font::Weight kSemibold = gfx::Font::Weight::SEMIBOLD;
inline constexpr gfx::Font::Weight kBold = gfx::Font::Weight::BOLD;
}  // namespace weight

// A run of Pane's text, as Paper sets one: the system font at a size and
// weight in a CSS line box (Tailwind's: text-xs is a 16-point line, text-sm
// a 20, leading-none the size itself), the text centered in it.
struct TextStyle {
  int size = 13;
  gfx::Font::Weight weight = weight::kRegular;
  // The line box's height; 0 takes the font's own.
  int line_height = 0;
};

// The styles Pane's controls share. Tracking and tabular digits have no
// Views path (Chromium shapes with no OpenType features,
// render_text_harfbuzz.cc): tracking goes without, and only the find bar's
// count keeps its tabular digits (DigitCellsLabel, below).
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
// A menu's row of Pane's own, 13 on a 16-point line (Paper 03.9, the
// extensions menus').
inline constexpr TextStyle kMenuRow{13, weight::kRegular, 16};
// A section's title over its card, 13 on 18 (Settings' groups, the profile
// sheet's sections).
inline constexpr TextStyle kSectionTitle{13, weight::kRegular, 18};
// A row's title, 13 on 18-point lines, and its description, 12 on 16
// (Settings' rows, the profile sheet's).
inline constexpr TextStyle kRowTitle{13, weight::kRegular, 18};
inline constexpr TextStyle kRowDescription{12, weight::kRegular, 16};
}  // namespace text

// The system font at `size` points and `weight`: SF on the Mac, the
// platform's UI font elsewhere (Paper 02.3's system stack). Each is made once.
const gfx::FontList& SystemFont(int size, gfx::Font::Weight weight);

// Each system's, which SystemFont makes once: the Mac's SF, its optical sizes
// and weights; on Windows 11 Segoe UI Variable, each weight on its axis and
// the optical size Windows gives each size, and on Windows 10 Segoe UI, as
// the user chose (text_win.cc).
gfx::FontList PlatformSystemFont(int size, gfx::Font::Weight weight);

// Whether Pane's labels take the system's subpixel text where it sits on an
// opaque ground: never on the Mac, by the user's call (ADR 0060); on
// Windows, ClearType, as the user chose (Paper 11.8, text_win.cc).
bool LabelsTakeClearType();

// The opaque color under `label`'s text, what its surface and the fills
// between paint there: where the system's text takes it (Windows), the label
// draws ClearType against it, painting it under its text in its own layer,
// which a transparent layer doesn't hold for ClearType to blend with. A
// ground that isn't opaque leaves it grayscale. On the Mac nothing changes.
// Pane's labels take the nearest ground up their parents themselves
// (ground.h), each theme.
void SetLabelGround(views::Label& label, SkColor ground);

// Sets `label` in Pane's text: the style's font and line box, `color` (a
// ui::ColorVariant, so a Theme change resolves it again), taken as it is
// rather than shifted for contrast, and grayscale rather than subpixel (ADR
// 0060). Skia still smooths it on the Mac, 10 to 18% heavier than text drawn
// unsmoothed, as the user accepted (ADR 0060).
void StyleLabel(views::Label& label,
                const TextStyle& style,
                ui::ColorVariant color);
std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        const TextStyle& style,
                                        ui::ColorVariant color);

// Lets a screen reader reach `view`, text that says something inside a view
// that holds the keys (the PIN screen, Settings, a dialog): outside ChromeOS,
// Chromium leaves out every view that can't take the focus under one that can
// (ViewAccessibility::UpdateIgnoredState), as a button's own label, so
// VoiceOver read none of their text. It takes the focus in accessibility mode
// alone, as the body text of Chromium's own dialogs does.
void ExposeToScreenReaders(views::View& view);

// A count in Pane's text whose digits each sit in a cell as wide as the
// font's figures (its zero), centered in it, so it holds still as it counts,
// as tabular digits would: the find bar's match count, the one
// text that keeps its tabular digits (Paper 10.6, option 1). Views shapes
// with HarfBuzz's default features alone (render_text_harfbuzz.cc), so the
// cells are Pane's, with no ui/gfx edit; what isn't a digit takes its own
// width, and the kerning beside a digit goes, as everywhere (ADR 0060).
class DigitCellsLabel : public views::View {
  METADATA_HEADER(DigitCellsLabel, views::View)

 public:
  DigitCellsLabel(const TextStyle& style, ui::ColorVariant color);
  DigitCellsLabel(const DigitCellsLabel&) = delete;
  DigitCellsLabel& operator=(const DigitCellsLabel&) = delete;
  ~DigitCellsLabel() override;

  void SetText(const std::u16string& text);
  const std::u16string& text() const { return text_; }
  void SetColor(ui::ColorVariant color);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  // Each character's own text, laid out once per text, and where it starts
  // in the label, in points.
  struct Glyph {
    Glyph();
    Glyph(Glyph&&);
    Glyph& operator=(Glyph&&);
    ~Glyph();

    std::unique_ptr<gfx::RenderText> text;
    float x = 0;
    float width = 0;
  };

  void Shape();
  // Each character's text in the color, as the view's provider resolves it.
  void ApplyColor();

  const TextStyle style_;
  ui::ColorVariant color_;
  std::u16string text_;
  std::vector<Glyph> glyphs_;
  float width_ = 0;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_TEXT_H_
