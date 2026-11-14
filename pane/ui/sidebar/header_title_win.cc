// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/header_title.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"

namespace pane {

namespace {

// Paper 11.4's A and 11.5: 24 points tall, 8 in from the header's edge, so
// its title starts 16 in, as the Mac's starts after the lights; "Pane" 12
// semibold, then a 12-point chevron 4 after it and 6 before its end, both
// muted; rounded 7, the accent fading in under the pointer and held while
// its menu shows. Its menu opens 2 under it.
constexpr int kHeight = 24;
constexpr int kLeading = 8;
constexpr int kPaddingStart = 8;
constexpr int kPaddingEnd = 6;
constexpr int kGap = 4;
constexpr int kChevron = 12;
constexpr int kMenuGap = 2;
constexpr TextStyle kTitle{12, weight::kSemibold, 16};

ControlLook MenuButtonLook() {
  ControlLook look(ControlShape::Squircle(radius::kSm));
  look.hover_fill = kPaneColorAccent;
  look.tint = kPaneColorMutedForeground;
  look.lit_tint = kPaneColorMutedForeground;
  // Under a contrast theme, a button's edge at rest too (Paper 11.10); clear
  // otherwise.
  look.border = kPaneColorForcedEdge;
  return look;
}

class HeaderMenuButton : public views::Button {
  METADATA_HEADER(HeaderMenuButton, views::Button)

 public:
  explicit HeaderMenuButton(
      base::RepeatingCallback<void(const gfx::Rect&, ui::mojom::MenuSourceType)>
          open_menu)
      : views::Button(base::BindRepeating(&HeaderMenuButton::Pressed,
                                          base::Unretained(this))),
        open_menu_(std::move(open_menu)) {
    // A menu opens as the press goes down, as Chrome's own menu's button.
    button_controller()->set_notify_action(
        views::ButtonController::NotifyAction::kOnPress);
    // Not a Tab stop, as Chrome's own menu's button isn't (ToolbarButton):
    // Alt and F10 open its menu from the keys. A Tab stop here, the first in
    // the window, took the keys with its ring whenever the page that had
    // them went, as a new tab's comes, Views moving them to the next stop.
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    highlight_ = std::make_unique<ControlHighlight>(
        this, look_.shape, look_.disabled_opacity,
        base::BindRepeating(&HeaderMenuButton::SchedulePaint,
                            base::Unretained(this)));
    SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
    title_ = AddChildView(MakeLabel(u"Pane", kTitle, look_.tint.value()));
    title_->SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kPopUpButton);
    GetViewAccessibility().SetName(u"Pane");
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
  }

  void SetMenuShowing(bool showing) {
    highlight_->SetHeld(showing);
    if (showing) {
      GetViewAccessibility().SetIsExpanded();
    } else {
      GetViewAccessibility().SetIsCollapsed();
    }
  }

  void OpenFromKeys() { Open(ui::mojom::MenuSourceType::kKeyboard); }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kPaddingStart + title_->GetPreferredSize().width() + kGap +
                         kChevron + kPaddingEnd,
                     kHeight);
  }

  void Layout(PassKey) override {
    title_->SetBounds(kPaddingStart, (height() - kTitle.line_height) / 2,
                      title_->GetPreferredSize().width(), kTitle.line_height);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    PaintIcon(*canvas, Icon::kChevronDown,
              gfx::RectF(title_->bounds().right() + kGap,
                         (height() - kChevron) / 2.0f, kChevron, kChevron),
              /*stroke_width=*/2, colors->GetColor(kPaneColorMutedForeground));
  }

 private:
  void Pressed(const ui::Event& event) {
    Open(event.IsKeyEvent() ? ui::mojom::MenuSourceType::kKeyboard
                            : ui::mojom::MenuSourceType::kMouse);
  }

  void Open(ui::mojom::MenuSourceType source) {
    gfx::Rect on_screen = GetBoundsInScreen();
    on_screen.Inset(gfx::Insets::TLBR(0, 0, -kMenuGap, 0));
    open_menu_.Run(on_screen, source);
  }

  const ControlLook look_ = MenuButtonLook();
  base::RepeatingCallback<void(const gfx::Rect&, ui::mojom::MenuSourceType)>
      open_menu_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> title_ = nullptr;
};

BEGIN_METADATA(HeaderMenuButton)
END_METADATA

}  // namespace

std::unique_ptr<views::View> MakeHeaderTitle(
    base::RepeatingCallback<void(const gfx::Rect&, ui::mojom::MenuSourceType)>
        open_menu) {
  return std::make_unique<HeaderMenuButton>(std::move(open_menu));
}

gfx::Rect HeaderTitleBounds(views::View& title, int row, int end) {
  const gfx::Size size = title.GetPreferredSize();
  return gfx::Rect(kLeading, (row - size.height()) / 2,
                   std::min(size.width(), std::max(end - kLeading, 0)),
                   size.height());
}

void SetHeaderMenuShowing(views::View& title, bool showing) {
  views::AsViewClass<HeaderMenuButton>(&title)->SetMenuShowing(showing);
}

bool OpenHeaderMenu(views::View& title) {
  if (!title.IsDrawn()) {
    return false;
  }
  views::AsViewClass<HeaderMenuButton>(&title)->OpenFromKeys();
  return true;
}

}  // namespace pane
