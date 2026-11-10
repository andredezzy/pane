// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text_button.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/squircle.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

TextButton::TextButton(PressedCallback pressed,
                       const std::u16string& title,
                       ButtonVariant variant,
                       const ButtonSize& size)
    : views::Button(std::move(pressed)),
      look_(ButtonLook(variant)),
      size_(size),
      border_(variant == ButtonVariant::kOutline ? 1 : 0) {
  highlight_ = std::make_unique<ControlHighlight>(
      this, look_.shape, look_.disabled_opacity,
      base::BindRepeating(&TextButton::Tint, base::Unretained(this)));
  SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
  // Its tint comes from the look, as lit as the hover is.
  title_ = AddChildView(MakeLabel(title, size_.text, kPaneColorForeground));
  title_->SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetName(title);
}

TextButton::~TextButton() = default;

void TextButton::SetTitle(const std::u16string& title) {
  if (title_->GetText() == title) {
    return;
  }
  title_->SetText(title);
  GetViewAccessibility().SetName(title);
  PreferredSizeChanged();
}

gfx::Size TextButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(size_.leading + title_->GetPreferredSize().width() +
                       size_.trailing + 2 * border_,
                   size_.height);
}

void TextButton::Layout(PassKey) {
  title_->SetBounds(
      size_.leading + border_, 0,
      std::max(width() - size_.leading - size_.trailing - 2 * border_, 0),
      height());
}

void TextButton::OnThemeChanged() {
  views::Button::OnThemeChanged();
  Tint();
}

void TextButton::Tint() {
  // The highlight steps from its making, before the title is there.
  if (!title_) {
    return;
  }
  if (const ui::ColorProvider* colors = GetColorProvider()) {
    if (const std::optional<SkColor> tint =
            TintOf(look_, *colors, highlight_->hover())) {
      title_->SetEnabledColor(*tint);
    }
  }
  SchedulePaint();
}

BEGIN_METADATA(TextButton)
END_METADATA

namespace {

// KeyedLabel's measures, in its button's `.keyed` size.
constexpr int kKeyedHeight = 36;
constexpr int kKeyedLeading = 14;
constexpr int kKeyedTrailing = 8;
constexpr int kKeyedSpacing = 8;
constexpr int kChipPadding = 6;
constexpr int kChipHeight = 20;
constexpr TextStyle kKeyText{12, weight::kMedium, kChipHeight};

}  // namespace

KeyedButton::KeyedButton(PressedCallback pressed,
                         const std::u16string& title,
                         const std::u16string& key)
    : views::Button(std::move(pressed)),
      look_(ButtonLook(ButtonVariant::kPrimary, radius::kLg)) {
  highlight_ = std::make_unique<ControlHighlight>(
      this, look_.shape, look_.disabled_opacity,
      base::BindRepeating(&views::View::SchedulePaint, base::Unretained(this)));
  SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
  // The title keeps its color under the pointer, as KeyedLabel's set its own.
  title_ = AddChildView(
      MakeLabel(title, text::kButton, kPaneColorPrimaryForeground));
  title_->SetCanProcessEventsWithinSubtree(false);
  key_ = AddChildView(MakeLabel(key, kKeyText, kPaneColorPrimaryKey));
  key_->SetCanProcessEventsWithinSubtree(false);
  // The key is the chip's, which assistive technologies don't read.
  key_->GetViewAccessibility().SetIsIgnored(true);
  GetViewAccessibility().SetName(title);
}

KeyedButton::~KeyedButton() = default;

gfx::Size KeyedButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kKeyedLeading + title_->GetPreferredSize().width() +
                       kKeyedSpacing + kChipPadding +
                       key_->GetPreferredSize().width() + kChipPadding +
                       kKeyedTrailing,
                   kKeyedHeight);
}

gfx::RectF KeyedButton::ChipBounds() const {
  const int chip = kChipPadding * 2 + key_->GetPreferredSize().width();
  return gfx::RectF(width() - kKeyedTrailing - chip,
                    (height() - kChipHeight) / 2.0f, chip, kChipHeight);
}

void KeyedButton::Layout(PassKey) {
  const gfx::RectF chip = ChipBounds();
  title_->SetBounds(
      kKeyedLeading, 0,
      std::max(static_cast<int>(chip.x()) - kKeyedSpacing - kKeyedLeading, 0),
      height());
  key_->SetBounds(chip.x() + kChipPadding, chip.y(),
                  chip.width() - 2 * kChipPadding, kChipHeight);
}

void KeyedButton::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorPrimaryKeyChip));
  canvas->DrawPath(SquirclePath(ChipBounds(), radius::kTab), flags);
}

BEGIN_METADATA(KeyedButton)
END_METADATA

}  // namespace pane
