// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TOOLBAR_TOOLBAR_PROGRESS_BAR_H_
#define PANE_UI_TOOLBAR_TOOLBAR_PROGRESS_BAR_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/views/view.h"

namespace pane {

namespace model {
class ToolbarProgress;
}

// The address field's 2-point load progress, drawn as Chrome's toolbar draws
// it (model::ToolbarProgress, ADR 0081) in the Electron app's blue-500 to
// blue-400, along the field's bottom edge and cut by its squircle, as the
// Swift clipped it. Each change draws at once. While a fade or the slow mode
// runs, the compositor's frames step it; a timer wakes it for what falls due
// between them (the slow mode after five quiet seconds, the fade after a
// finish's 100 ms). On a layer of its own: a fade changes the layer's
// opacity, and a step repaints the bar's strip alone.
class ToolbarProgressBar : public views::View,
                           public ui::CompositorAnimationObserver {
  METADATA_HEADER(ToolbarProgressBar, views::View)

 public:
  static constexpr int kHeight = 2;

  // `progress`, the app's, outlives the bar.
  explicit ToolbarProgressBar(model::ToolbarProgress* progress);
  ToolbarProgressBar(const ToolbarProgressBar&) = delete;
  ToolbarProgressBar& operator=(const ToolbarProgressBar&) = delete;
  ~ToolbarProgressBar() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  // What the progress says at `now`, with the frames and the wake it needs.
  void Draw(base::TimeTicks now);
  void Changed();
  void StopFrames();

  const raw_ptr<model::ToolbarProgress> progress_;
  // The compositor stepping the bar, while it does.
  raw_ptr<ui::Compositor> compositor_ = nullptr;
  base::OneShotTimer wake_;
  // When the bar was last drawn: a step's frame time, or a change's now.
  base::TimeTicks drawn_at_;
  // Inside a step: its frame time.
  std::optional<base::TimeTicks> stepping_at_;
};

}  // namespace pane

#endif  // PANE_UI_TOOLBAR_TOOLBAR_PROGRESS_BAR_H_
