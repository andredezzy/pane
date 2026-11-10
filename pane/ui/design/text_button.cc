// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text_button.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
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

}  // namespace pane
