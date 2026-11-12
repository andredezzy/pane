// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/sad_tab_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/placement.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/compositor.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The title, 14 medium on a 20-point line in the accent's foreground, 4 over
// the message, 12 on a 16-point line, muted; the button 12 under them.
constexpr TextStyle kTitle{14, weight::kMedium, 20};
constexpr TextStyle kMessage{12, weight::kRegular, 16};
constexpr int kLineSpacing = 4;
constexpr int kButtonSpacing = 12;

}  // namespace

SadTabView::SadTabView(base::RepeatingClosure reload)
    : reload_(std::move(reload)) {
  SetID(kViewIdSadTab);
  // It takes the keys in the page's place, with no ring of its own.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"This page crashed");
  title_ = AddChildView(
      MakeLabel(u"This page crashed", kTitle, kPaneColorAccentForeground));
  message_ = AddChildView(
      MakeLabel(u"Something went wrong while displaying this page.", kMessage,
                kPaneColorMutedForeground));
  // Read after the name the title gives it, under the view that holds the
  // keys; the title, its name, isn't read twice.
  ExposeToScreenReaders(*message_);
  button_ = AddChildView(std::make_unique<TextButton>(
      base::BindRepeating([](SadTabView* view) { view->reload_.Run(); },
                          base::Unretained(this)),
      u"Reload", ButtonVariant::kOutline));
  button_->SetID(kViewIdSadTabReload);
}

SadTabView::~SadTabView() = default;

// One column in the middle of the page's place, each line centered in it, as
// SwiftUI centers them: on half points where the middle falls on one.
void SadTabView::Layout(PassKey) {
  const gfx::Size title = title_->GetPreferredSize();
  const gfx::Size message = message_->GetPreferredSize();
  const gfx::Size button = button_->GetPreferredSize();
  const float height = kTitle.line_height + kLineSpacing +
                       kMessage.line_height + kButtonSpacing + button.height();
  const float top = (this->height() - height) / 2.0f;
  const float middle = width() / 2.0f;
  const float scale = GetWidget() && GetWidget()->GetCompositor()
                          ? GetWidget()->GetCompositor()->device_scale_factor()
                          : 1;
  const auto centered = [&](const gfx::Size& size, float y) {
    return gfx::RectF(middle - size.width() / 2.0f, y, size.width(),
                      size.height());
  };
  PlaceOnPixels(*title_,
                centered(gfx::Size(title.width(), kTitle.line_height), top),
                scale);
  PlaceOnPixels(*message_,
                centered(gfx::Size(message.width(), kMessage.line_height),
                         top + kTitle.line_height + kLineSpacing),
                scale);
  PlaceOnPixels(*button_,
                centered(button, top + kTitle.line_height + kLineSpacing +
                                     kMessage.line_height + kButtonSpacing),
                scale);
}

bool SadTabView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() != ui::VKEY_RETURN || event.flags() != ui::EF_NONE) {
    return views::View::OnKeyPressed(event);
  }
  reload_.Run();
  return true;
}

BEGIN_METADATA(SadTabView)
END_METADATA

}  // namespace pane
