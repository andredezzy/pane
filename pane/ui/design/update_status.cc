// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/update_status.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/palette.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The dot at half, at the bottom of its breath.
constexpr double kPulseLow = 0.5;

}  // namespace

UpdateDot::UpdateDot()
    : pulse_(1,
             base::BindRepeating(&UpdateDot::Pulsed, base::Unretained(this))) {
  // The emerald dot, the same in both appearances.
  SetBackground(
      views::CreateRoundedRectBackground(kPaneColorSuccess, kSize / 2.0f));
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
}

UpdateDot::~UpdateDot() = default;

void UpdateDot::SetPulsing(bool pulsing) {
  if (pulsing != pulsing_) {
    pulsing_ = pulsing;
    UpdatePulse();
  }
}

gfx::Size UpdateDot::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kSize, kSize);
}

void UpdateDot::AddedToWidget() {
  UpdatePulse();
}

bool UpdateDot::Pulses() {
  const views::Widget* widget = GetWidget();
  return pulsing_ && widget && widget->GetCompositor() && !motion::IsReduced();
}

void UpdateDot::UpdatePulse() {
  if (Pulses()) {
    if (!pulse_.is_moving()) {
      pulse_.MoveTo(pulse_.value() < 1 ? 1 : kPulseLow, motion::Pulse(),
                    GetWidget()->GetCompositor());
    }
    return;
  }
  pulse_.MoveTo(1, motion::Pulse(), nullptr);
}

// Tailwind's animate-pulse: to half and back, every two seconds, for as long
// as the download runs. Each half starts once the last has landed.
void UpdateDot::Pulsed(double opacity) {
  layer()->SetOpacity(opacity);
  if (pulse_.is_moving() || !Pulses()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&UpdateDot::UpdatePulse, weak_factory_.GetWeakPtr()));
}

BEGIN_METADATA(UpdateDot)
END_METADATA

UpdateBar::UpdateBar()
    : share_(0,
             base::BindRepeating(
                 [](UpdateBar* bar, double) { bar->SchedulePaint(); },
                 base::Unretained(this))) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
}

UpdateBar::~UpdateBar() = default;

void UpdateBar::SetShare(double share, ::ui::Compositor* compositor) {
  share_.MoveTo(std::clamp(share, 0.0, 1.0), motion::Progress(), compositor);
}

void UpdateBar::SetLeading(float leading) {
  if (leading != leading_) {
    leading_ = leading;
    SchedulePaint();
  }
}

void UpdateBar::Reset() {
  share_.MoveTo(0, motion::Progress(), nullptr);
}

void UpdateBar::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  const ControlShape capsule = ControlShape::Capsule();
  gfx::RectF track(GetLocalBounds());
  track.Inset(gfx::InsetsF::TLBR(0, leading_, 0, 0));
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorUpdateTrack));
  canvas->DrawPath(capsule.PathIn(track), flags);
  if (share_.value() <= 0) {
    return;
  }
  flags.setColor(colors->GetColor(kPaneColorSuccess));
  canvas->DrawPath(capsule.PathIn(gfx::RectF(track.x(), track.y(),
                                             track.width() * share_.value(),
                                             track.height())),
                   flags);
}

BEGIN_METADATA(UpdateBar)
END_METADATA

}  // namespace pane
