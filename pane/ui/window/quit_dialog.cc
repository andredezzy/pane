// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/quit_dialog.h"

#include <utility>

#include "cc/paint/paint_flags.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace pane {

namespace {

// The content, inside the card's 24 of padding.
constexpr int kContentWidth = QuitDialog::kWidth - 2 * 24;
// VStack's spacing, and the buttons' row 8 further down.
constexpr int kSpacing = 16;
constexpr int kButtonsTop = 8;
constexpr int kButtonSpacing = 8;
// The tile: 40 points, rounded 18; its icon 22, stroked 1.75.
constexpr int kTileSize = 40;
constexpr float kTileRadius = 18;
constexpr int kIconSize = 22;
constexpr float kIconStroke = 1.75f;

}  // namespace

// The icon's tile, which assistive technologies skip, as the Swift's did.
class QuitDialog::Tile : public views::View {
  METADATA_HEADER(Tile, views::View)

 public:
  Tile() { GetViewAccessibility().SetIsIgnored(true); }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorIconTile));
    canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), kTileRadius),
                     flags);
    const float inset = (kTileSize - kIconSize) / 2.0f;
    PaintIcon(*canvas, Icon::kCircleDashedX,
              gfx::RectF(inset, inset, kIconSize, kIconSize), kIconStroke,
              colors->GetColor(kPaneColorForeground));
  }
};

BEGIN_METADATA(QuitDialog, Tile)
END_METADATA

QuitDialog::QuitDialog(base::RepeatingClosure cancel,
                       base::RepeatingClosure quit) {
  tile_ = AddChildView(std::make_unique<Tile>());
  heading_ = AddChildView(std::make_unique<DialogHeading>(
      u"Quit Pane?",
      u"Your profiles and tabs will be here when you open Pane again. "
      u"Downloads in progress will stop.",
      kPaneColorDialogMessage));
  cancel_ = AddChildView(std::make_unique<KeyedButton>(
      std::move(cancel), u"Cancel", u"Esc", KeyedButton::Role::kCancel));
  cancel_->SetID(kViewIdQuitCancel);
  quit_ = AddChildView(std::make_unique<KeyedButton>(
      std::move(quit), u"Quit", u"⌘Q", KeyedButton::Role::kDestructive));
  quit_->SetID(kViewIdQuitConfirm);
}

QuitDialog::~QuitDialog() = default;

gfx::Size QuitDialog::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : kContentWidth;
  return gfx::Size(width, kTileSize + kSpacing +
                              heading_->GetHeightForWidth(width) + kSpacing +
                              kButtonsTop + quit_->GetPreferredSize().height());
}

void QuitDialog::Layout(PassKey) {
  tile_->SetBounds(0, 0, kTileSize, kTileSize);
  const int heading_top = kTileSize + kSpacing;
  const int heading = heading_->GetHeightForWidth(width());
  heading_->SetBounds(0, heading_top, width(), heading);
  const int buttons_top = heading_top + heading + kSpacing + kButtonsTop;
  const gfx::Size quit = quit_->GetPreferredSize();
  const gfx::Size cancel = cancel_->GetPreferredSize();
  quit_->SetBounds(width() - quit.width(), buttons_top, quit.width(),
                   quit.height());
  cancel_->SetBounds(quit_->x() - kButtonSpacing - cancel.width(), buttons_top,
                     cancel.width(), cancel.height());
}

BEGIN_METADATA(QuitDialog)
END_METADATA

}  // namespace pane
