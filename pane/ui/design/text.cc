// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "pane/ui/design/ground.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/class_properties.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// A label that keeps its laid-out text while hidden. views::Label drops it at
// every hide, which saves a hidden window's labels their memory, but Pane's
// panels and dialogs hide and show again: each showing shaped every label
// over on the UI thread in its first frame, Settings' 30 of them, and the
// App icon picker's ↩ through 41 fallback fonts for 2 ms. Kept, the next
// showing draws them as they were laid out; any change to the text, its
// style, or its bounds lays it out again, as before.
// The opaque ground a label of Pane's paints under its text and takes
// ClearType against, where the system's text takes it (SetLabelGround): an
// SkColor's bits, as the class properties hold ints.
DEFINE_UI_CLASS_PROPERTY_KEY(int, kTextGroundKey, 0)

SkColor TextGroundOf(const views::View& label) {
  return static_cast<SkColor>(label.GetProperty(kTextGroundKey));
}

class KeptLabel : public views::Label {
 public:
  using views::Label::Label;

 protected:
  // views::View:
  void VisibilityChanged(views::View* starting_from, bool is_visible) override {
  }

  // Its text's ground, the nearest up its parents, for this theme
  // (ground.h).
  void OnThemeChanged() override {
    views::Label::OnThemeChanged();
    SetLabelGround(*this, GroundOf(*this));
  }

  // Under its text, its ground where it takes one (SetLabelGround): the
  // text's own box and a little past it, for ClearType's fringes, within the
  // label, so a control's corners and borders around it keep their paint.
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const SkColor ground = TextGroundOf(*this);
    if (SkColorGetA(ground) == SK_AlphaOPAQUE) {
      gfx::Rect text = GetTextBounds();
      text.Outset(gfx::Outsets::VH(1, 2));
      text.Intersect(GetLocalBounds());
      canvas->FillRect(text, ground);
    }
    views::Label::OnPaintBackground(canvas);
  }
};

}  // namespace

const gfx::FontList& SystemFont(int size, gfx::Font::Weight weight) {
  static base::NoDestructor<
      std::map<std::pair<int, gfx::Font::Weight>, gfx::FontList>>
      fonts;
  const std::pair key(size, weight);
  auto found = fonts->find(key);
  if (found == fonts->end()) {
    found = fonts->emplace(key, PlatformSystemFont(size, weight)).first;
  }
  return found->second;
}

void SetLabelGround(views::Label& label, SkColor ground) {
  const bool takes =
      LabelsTakeClearType() && SkColorGetA(ground) == SK_AlphaOPAQUE;
  const SkColor kept = takes ? ground : SK_ColorTRANSPARENT;
  // Nothing changes where none is taken or kept: the Mac's labels, always.
  if (TextGroundOf(label) == kept) {
    return;
  }
  label.SetProperty(kTextGroundKey, static_cast<int>(kept));
  label.SetBackgroundColor(kept);
  label.SetSubpixelRenderingEnabled(takes);
  label.SetSkipSubpixelRenderingOpacityCheck(true);
  label.SchedulePaint();
}

void StyleLabel(views::Label& label,
                const TextStyle& style,
                ui::ColorVariant color) {
  label.SetFontList(SystemFont(style.size, style.weight));
  label.SetLineHeight(style.line_height);
  label.SetAutoColorReadabilityEnabled(false);
  label.SetEnabledColor(std::move(color));
  // Grayscale until its ground is named (SetLabelGround).
  label.SetSubpixelRenderingEnabled(false);
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        const TextStyle& style,
                                        ui::ColorVariant color) {
  auto label = std::make_unique<KeptLabel>(text);
  StyleLabel(*label, style, std::move(color));
  return label;
}

void ExposeToScreenReaders(views::View& view) {
  view.SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
}

DigitCellsLabel::Glyph::Glyph() = default;
DigitCellsLabel::Glyph::Glyph(Glyph&&) = default;
DigitCellsLabel::Glyph& DigitCellsLabel::Glyph::operator=(Glyph&&) = default;
DigitCellsLabel::Glyph::~Glyph() = default;

DigitCellsLabel::DigitCellsLabel(const TextStyle& style, ui::ColorVariant color)
    : style_(style), color_(std::move(color)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
  // Text that changes as it counts, read by its name.
  GetViewAccessibility().SetName(
      std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
}

DigitCellsLabel::~DigitCellsLabel() = default;

void DigitCellsLabel::SetText(const std::u16string& text) {
  if (text == text_) {
    return;
  }
  text_ = text;
  if (text_.empty()) {
    GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
  } else {
    GetViewAccessibility().SetName(text_);
  }
  Shape();
  PreferredSizeChanged();
  SchedulePaint();
}

void DigitCellsLabel::SetColor(ui::ColorVariant color) {
  color_ = std::move(color);
  ApplyColor();
}

// Each character on its own, as a label would draw it alone: a digit
// centered in a cell as wide as a figure, the zero's width, as a font draws
// its tabular figures (SF Pro's are its zero's 7.559 points at 12, where its
// widest digit, the four, is 7.723), anything else at its own width.
void DigitCellsLabel::Shape() {
  glyphs_.clear();
  const gfx::FontList& font = SystemFont(style_.size, style_.weight);
  const float cell = gfx::GetStringWidthF(u"0", font);
  float x = 0;
  for (const char16_t character : text_) {
    const std::u16string one(1, character);
    const float width = gfx::GetStringWidthF(one, font);
    Glyph glyph;
    glyph.text = gfx::RenderText::CreateRenderText();
    glyph.text->SetFontList(font);
    glyph.text->SetText(one);
    glyph.text->SetMinLineHeight(style_.line_height);
    glyph.text->set_subpixel_rendering_suppressed(true);
    glyph.width = width;
    const bool digit = base::IsAsciiDigit(character);
    glyph.x = digit ? x + (cell - width) / 2 : x;
    x += digit ? cell : width;
    glyphs_.push_back(std::move(glyph));
  }
  width_ = x;
  ApplyColor();
}

gfx::Size DigitCellsLabel::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const gfx::FontList& font = SystemFont(style_.size, style_.weight);
  return gfx::Size(std::ceil(width_),
                   std::max(style_.line_height, font.GetHeight()));
}

void DigitCellsLabel::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyColor();
}

void DigitCellsLabel::ApplyColor() {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  const SkColor color = color_.ResolveToSkColor(colors);
  for (Glyph& glyph : glyphs_) {
    glyph.text->SetColor(color);
  }
  SchedulePaint();
}

void DigitCellsLabel::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  // On the screen's pixels: a glyph's place past whole points stays, at the
  // scale the canvas draws.
  const float scale = canvas->image_scale();
  for (Glyph& glyph : glyphs_) {
    const float x = std::round(glyph.x * scale) / scale;
    const float whole = std::floor(x);
    glyph.text->SetDisplayRect(gfx::Rect(static_cast<int>(whole), 0,
                                         std::ceil(glyph.width) + 1, height()));
    gfx::ScopedCanvas scoped(canvas);
    canvas->sk_canvas()->translate(x - whole, 0);
    glyph.text->Draw(canvas);
  }
}

BEGIN_METADATA(DigitCellsLabel)
END_METADATA

}  // namespace pane
