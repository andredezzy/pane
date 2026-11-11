// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/focus_ring.h"

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_class_properties.h"

namespace pane {

// static
FocusRing* FocusRing::Install(views::View* control, ControlShape shape) {
  auto ring = base::WrapUnique(new FocusRing(control, shape));
  ring->SetProperty(views::kViewIgnoredByLayoutKey, true);
  return control->AddChildView(std::move(ring));
}

FocusRing::FocusRing(views::View* control, ControlShape shape)
    : control_(control), shape_(shape) {
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  SetVisible(false);
  observation_.Observe(control);
}

FocusRing::~FocusRing() = default;

void FocusRing::SetShape(ControlShape shape) {
  shape_ = shape;
  SchedulePaint();
}

void FocusRing::SetShowsWhen(ShowsWhen shows_when) {
  shows_when_ = std::move(shows_when);
  Refresh();
}

void FocusRing::SetAround(Around around) {
  around_ = std::move(around);
  UpdateBounds();
}

void FocusRing::UpdateBounds() {
  gfx::Rect bounds =
      around_ ? around_.Run(control_) : control_->GetLocalBounds();
  bounds.Outset(kWidth);
  SetBoundsRect(bounds);
}

void FocusRing::SetFocusWithin(bool within) {
  within_ = within;
  Refresh();
}

void FocusRing::Refresh() {
  const views::FocusManager* focus_manager = control_->GetFocusManager();
  const views::View* focused =
      focus_manager ? focus_manager->GetFocusedView() : nullptr;
  const bool has_focus =
      within_ ? focused && control_->Contains(focused) : control_->HasFocus();
  const bool shows = control_->GetWidget() && has_focus &&
                     (!shows_when_ || shows_when_.Run(control_));
  if (shows == GetVisible()) {
    return;
  }
  SetVisible(shows);
  if (shows) {
    // A layer of its own, to draw past the control's edge.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    UpdateBounds();
    SchedulePaint();
  } else {
    DestroyLayer();
  }
}

void FocusRing::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(kWidth);
  flags.setColor(colors->GetColor(kPaneColorRing));
  canvas->DrawPath(
      shape_.Outset(kWidth).StrokePathIn(gfx::RectF(GetLocalBounds()), kWidth),
      flags);
}

void FocusRing::OnViewFocused(views::View* view) {
  Refresh();
}

void FocusRing::OnViewBlurred(views::View* view) {
  Refresh();
}

void FocusRing::OnViewBoundsChanged(views::View* view) {
  UpdateBounds();
}

void FocusRing::OnViewAddedToWidget(views::View* view) {
  Refresh();
}

BEGIN_METADATA(FocusRing)
END_METADATA

}  // namespace pane
