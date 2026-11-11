// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/lock/lock_update_status.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/update_status.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// HStack(spacing: 6), the bar 2 after the status and 64 wide, the time left
// 2 after it and at least 56 wide.
constexpr int kSpacing = 6;
constexpr int kBarLeading = 2;
constexpr int kBarWidth = 64;
constexpr int kEtaLeading = 2;
constexpr int kEtaMinimum = 56;
// The status at 12, the time left at 12, muted; the action at 12 medium,
// muted.
constexpr TextStyle kStatusText{12, weight::kRegular, 0};
constexpr TextStyle kActionText{12, weight::kMedium, 0};

}  // namespace

// The action: its title alone, as the Swift's plain button drew it.
class LockUpdateStatus::Action : public views::Button {
  METADATA_HEADER(Action, views::Button)

 public:
  explicit Action(PressedCallback pressed) : views::Button(std::move(pressed)) {
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    views::FocusRing::Remove(this);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0);
    title_ = AddChildView(
        MakeLabel(std::u16string(), kActionText, kPaneColorMutedForeground));
    title_->SetCanProcessEventsWithinSubtree(false);
    title_->GetViewAccessibility().SetIsIgnored(true);
  }

  void SetTitle(const std::u16string& title) {
    title_->SetText(title);
    GetViewAccessibility().SetName(title);
  }

  int TitleWidth() const { return title_->GetPreferredSize().width(); }

  // views::Button:
  void Layout(PassKey) override { title_->SetBoundsRect(GetLocalBounds()); }

 private:
  raw_ptr<views::Label> title_ = nullptr;
};

BEGIN_METADATA(LockUpdateStatus, Action)
END_METADATA

LockUpdateStatus::LockUpdateStatus(base::RepeatingClosure action)
    : action_shown_(0,
                    base::BindRepeating(
                        [](LockUpdateStatus* status, double share) {
                          status->action_->layer()->SetOpacity(share);
                        },
                        base::Unretained(this))) {
  SetID(kViewIdLockScreenUpdate);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  // The pointer over any of it, its action included.
  SetNotifyEnterExitOnChild(true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  dot_ = AddChildView(std::make_unique<UpdateDot>());
  label_ = AddChildView(
      MakeLabel(std::u16string(), kStatusText, kPaneColorAccentForeground));
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_->SetCanProcessEventsWithinSubtree(false);
  bar_ = AddChildView(std::make_unique<UpdateBar>());
  eta_ = AddChildView(
      MakeLabel(std::u16string(), kStatusText, kPaneColorMutedForeground));
  eta_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  eta_->SetCanProcessEventsWithinSubtree(false);
  action_ = AddChildView(std::make_unique<Action>(std::move(action)));
}

LockUpdateStatus::~LockUpdateStatus() = default;

void LockUpdateStatus::Set(const Shows& shows) {
  if (shows == shows_) {
    return;
  }
  const bool started = shows.downloading && !shows_.downloading;
  shows_ = shows;
  label_->SetText(shows.label);
  bar_->SetVisible(shows.downloading);
  eta_->SetVisible(shows.downloading);
  eta_->SetText(shows.eta);
  dot_->SetPulsing(shows.downloading);
  if (started) {
    bar_->Reset();
  }
  if (shows.downloading) {
    views::Widget* widget = GetWidget();
    bar_->SetShare(shows.progress, widget && widget->IsVisible()
                                       ? widget->GetCompositor()
                                       : nullptr);
  }
  action_->SetVisible(!shows.action.empty());
  action_->SetTitle(shows.action);
  action_->SetID(shows.action_id);
  PreferredSizeChanged();
  InvalidateLayout();
}

const views::View* LockUpdateStatus::action() const {
  return action_->GetVisible() ? action_.get() : nullptr;
}

int LockUpdateStatus::ActionWidth() const {
  return shows_.action.empty() ? 0 : action_->TitleWidth();
}

gfx::Size LockUpdateStatus::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int action = ActionWidth();
  int width = UpdateDot::kSize + kSpacing + label_->GetPreferredSize().width();
  if (action > 0) {
    width += 2 * (action + kSpacing);
  }
  if (shows_.downloading) {
    width += kSpacing + kBarLeading + kBarWidth + kSpacing + kEtaLeading +
             std::max(eta_->GetPreferredSize().width(), kEtaMinimum);
  }
  return gfx::Size(width, label_->GetPreferredSize().height());
}

void LockUpdateStatus::Layout(PassKey) {
  const int action = ActionWidth();
  const int line = height();
  int x = action > 0 ? action + kSpacing : 0;
  dot_->SetBounds(x, (line - UpdateDot::kSize) / 2, UpdateDot::kSize,
                  UpdateDot::kSize);
  x += UpdateDot::kSize + kSpacing;
  const int label_width = label_->GetPreferredSize().width();
  label_->SetBounds(x, 0, label_width, line);
  x += label_width;
  if (shows_.downloading) {
    x += kSpacing + kBarLeading;
    bar_->SetBounds(x, (line - UpdateBar::kHeight) / 2, kBarWidth,
                    UpdateBar::kHeight);
    x += kBarWidth + kSpacing + kEtaLeading;
    const int eta_width =
        std::max(eta_->GetPreferredSize().width(), kEtaMinimum);
    eta_->SetBounds(x, 0, eta_width, line);
    x += eta_width;
  }
  if (action > 0) {
    action_->SetBounds(x + kSpacing, 0, action, line);
  }
}

void LockUpdateStatus::OnMouseEntered(const ui::MouseEvent& event) {
  SetHovered(true);
}

void LockUpdateStatus::OnMouseExited(const ui::MouseEvent& event) {
  SetHovered(false);
}

void LockUpdateStatus::SetHovered(bool hovered) {
  if (hovered == hovered_) {
    return;
  }
  hovered_ = hovered;
  views::Widget* widget = GetWidget();
  action_shown_.MoveTo(hovered ? 1 : 0, motion::Hover(),
                       widget ? widget->GetCompositor() : nullptr);
}

BEGIN_METADATA(LockUpdateStatus)
END_METADATA

}  // namespace pane
