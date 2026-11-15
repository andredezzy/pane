// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/system_alert_dialog.h"

#include <memory>
#include <utility>

#include "pane/ui/design/dialog.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace pane {

namespace {

// The content, inside the card's 24 of padding.
constexpr int kContentWidth = SystemAlertDialog::kWidth - 2 * 24;
// VStack's spacing, and the buttons' row 8 further down, as Exit Pane?'s.
constexpr int kSpacing = 16;
constexpr int kButtonsTop = 8;
constexpr int kButtonSpacing = 8;

}  // namespace

SystemAlertDialog::SystemAlertDialog(const SystemAlert& alert,
                                     base::RepeatingClosure cancel,
                                     base::RepeatingClosure act)
    : alert_(alert) {
  tile_ = AddChildView(std::make_unique<DialogTile>(Icon::kTrash2));
  heading_ = AddChildView(std::make_unique<DialogHeading>(
      alert.title, alert.message, kPaneColorDialogMessage));
  cancel_ = AddChildView(std::make_unique<KeyedButton>(
      std::move(cancel), alert.cancel, u"Esc", KeyedButton::Role::kCancel));
  cancel_->SetID(kViewIdAlertCancel);
  // Destructive, and so no key's (Paper 11.13 A).
  action_ = AddChildView(std::make_unique<KeyedButton>(
      std::move(act), alert.action, std::u16string(),
      KeyedButton::Role::kDestructive));
  action_->SetID(kViewIdAlertAction);
}

SystemAlertDialog::~SystemAlertDialog() = default;

views::View* SystemAlertDialog::cancel_button() {
  return cancel_;
}

views::View* SystemAlertDialog::action_button() {
  return action_;
}

gfx::Size SystemAlertDialog::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : kContentWidth;
  return gfx::Size(
      width, DialogTile::kSize + kSpacing + heading_->GetHeightForWidth(width) +
                 kSpacing + kButtonsTop + action_->GetPreferredSize().height());
}

void SystemAlertDialog::Layout(PassKey) {
  tile_->SetBounds(0, 0, DialogTile::kSize, DialogTile::kSize);
  const int heading_top = DialogTile::kSize + kSpacing;
  const int heading = heading_->GetHeightForWidth(width());
  heading_->SetBounds(0, heading_top, width(), heading);
  const int buttons_top = heading_top + heading + kSpacing + kButtonsTop;
  const gfx::Size action = action_->GetPreferredSize();
  const gfx::Size cancel = cancel_->GetPreferredSize();
  action_->SetBounds(width() - action.width(), buttons_top, action.width(),
                     action.height());
  cancel_->SetBounds(action_->x() - kButtonSpacing - cancel.width(),
                     buttons_top, cancel.width(), cancel.height());
}

BEGIN_METADATA(SystemAlertDialog)
END_METADATA

}  // namespace pane
