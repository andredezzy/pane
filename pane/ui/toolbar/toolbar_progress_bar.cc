// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/toolbar/toolbar_progress_bar.h"

#include <algorithm>
#include <optional>

#include "base/functional/bind.h"
#include "base/location.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "pane/model/toolbar_progress.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/view_ids.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"

namespace pane {

ToolbarProgressBar::ToolbarProgressBar(model::ToolbarProgress* progress)
    : progress_(progress) {
  SetID(kViewIdLoadBar);
  // The field under it takes the presses, and reads the page's load.
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0);
  progress_->set_on_change(base::BindRepeating(&ToolbarProgressBar::Changed,
                                               base::Unretained(this)));
}

ToolbarProgressBar::~ToolbarProgressBar() {
  StopFrames();
  progress_->set_on_change(base::RepeatingClosure());
}

void ToolbarProgressBar::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  progress_->SetWidth(width());
  if (const views::Widget* widget = GetWidget();
      widget && widget->GetCompositor()) {
    progress_->set_scale(widget->GetCompositor()->device_scale_factor());
  }
  Draw(base::TimeTicks::Now());
}

void ToolbarProgressBar::AddedToWidget() {
  // What fell due while the bar was away (Settings covered the toolbar).
  progress_->Frame(base::TimeTicks::Now());
}

void ToolbarProgressBar::RemovedFromWidget() {
  StopFrames();
  wake_.Stop();
}

void ToolbarProgressBar::Changed() {
  Draw(stepping_at_.value_or(base::TimeTicks::Now()));
}

// What the progress says now, with no animation of the layer's own; then the
// frames and the wake it needs.
void ToolbarProgressBar::Draw(base::TimeTicks now) {
  drawn_at_ = now;
  layer()->SetOpacity(progress_->AlphaAt(now));
  SchedulePaint();

  ui::Compositor* compositor =
      GetWidget() ? GetWidget()->GetCompositor() : nullptr;
  if (compositor && progress_->NeedsFrames(now)) {
    if (compositor_ != compositor) {
      StopFrames();
      compositor_ = compositor;
      compositor_->AddAnimationObserver(this);
    }
  } else {
    StopFrames();
  }

  wake_.Stop();
  if (const std::optional<base::TimeTicks> deadline = progress_->NextDeadline();
      deadline && compositor) {
    wake_.Start(FROM_HERE, std::max(*deadline - now, base::TimeDelta()),
                base::BindOnce(
                    [](ToolbarProgressBar* bar) {
                      bar->progress_->Frame(base::TimeTicks::Now());
                    },
                    base::Unretained(this)));
  }
}

void ToolbarProgressBar::StopFrames() {
  if (compositor_ && compositor_->HasAnimationObserver(this)) {
    compositor_->RemoveAnimationObserver(this);
  }
  compositor_ = nullptr;
}

void ToolbarProgressBar::OnAnimationStep(base::TimeTicks timestamp) {
  stepping_at_ = timestamp;
  progress_->Frame(timestamp);
  stepping_at_.reset();
}

void ToolbarProgressBar::OnCompositingShuttingDown(ui::Compositor* compositor) {
  StopFrames();
}

// The fill, the blue-500 to blue-400 gradient across its own width, as the
// Swift's CAGradientLayer spanned its frame; the shimmer over it; both cut
// by the field's squircle, whose bottom edge the bar runs along.
void ToolbarProgressBar::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors || !parent()) {
    return;
  }
  canvas->Save();
  canvas->ClipPath(
      SquirclePath(gfx::RectF(parent()->GetLocalBounds()), radius::kTab)
          .makeOffset(-x(), -y()),
      /*do_anti_alias=*/true);

  const float fill = width() * progress_->progress();
  if (fill > 0) {
    const SkPoint points[] = {{0, 0}, {fill, 0}};
    const SkColor4f stops[] = {
        SkColor4f::FromColor(colors->GetColor(kPaneColorLoadBarStart)),
        SkColor4f::FromColor(colors->GetColor(kPaneColorLoadBarEnd))};
    cc::PaintFlags flags;
    flags.setShader(cc::PaintShader::MakeLinearGradient(points, stops, nullptr,
                                                        2, SkTileMode::kClamp));
    canvas->DrawRect(gfx::RectF(0, 0, fill, height()), flags);
  }

  if (const std::optional<model::ToolbarProgress::ShimmerSpan> shimmer =
          progress_->ShimmerAt(drawn_at_)) {
    cc::PaintFlags flags;
    flags.setColor(colors->GetColor(kPaneColorLoadShimmer));
    flags.setAlphaf(static_cast<float>(std::clamp(shimmer->alpha, 0.0, 1.0)));
    canvas->DrawRect(
        gfx::RectF(shimmer->from, 0, std::max(shimmer->to - shimmer->from, 0.0),
                   height()),
        flags);
  }
  canvas->Restore();
}

BEGIN_METADATA(ToolbarProgressBar)
END_METADATA

}  // namespace pane
