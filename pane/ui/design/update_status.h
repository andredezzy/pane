// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_UPDATE_STATUS_H_
#define PANE_UI_DESIGN_UPDATE_STATUS_H_

#include "base/memory/weak_ptr.h"
#include "pane/ui/design/motion.h"
#include "ui/views/view.h"

namespace ui {
class Compositor;
}

namespace pane {

// What the update's status shows wherever it shows (DESIGN.md › Updates: the
// sidebar's pill and the Lock read the one Updater): the Swift's UpdateDot and
// UpdateProgressBar, which the data's one-time move on the Lock's panel takes
// too (Paper 07.5).

// The status's dot: 6 points, emerald in both appearances. While the dmg
// downloads it breathes to half and back every two seconds (Tailwind's
// animate-pulse, Motion.pulse), in a window that draws, unless the system
// asks for less motion. On a layer of its own, so its breath repaints
// nothing.
class UpdateDot : public views::View {
  METADATA_HEADER(UpdateDot, views::View)

 public:
  static constexpr int kSize = 6;

  UpdateDot();
  UpdateDot(const UpdateDot&) = delete;
  UpdateDot& operator=(const UpdateDot&) = delete;
  ~UpdateDot() override;

  void SetPulsing(bool pulsing);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void AddedToWidget() override;

 private:
  bool Pulses();
  void UpdatePulse();
  void Pulsed(double opacity);

  bool pulsing_ = false;
  ValueMotion pulse_;
  base::WeakPtrFactory<UpdateDot> weak_factory_{this};
};

// A download's bar: a 4-point track, the foreground at 10%, with the emerald
// share downloaded, moving to each new share on Motion.progress. On a layer of
// its own. Its track can start a fraction of a point into the view, where the
// text before it ends (the pill's).
class UpdateBar : public views::View {
  METADATA_HEADER(UpdateBar, views::View)

 public:
  static constexpr int kHeight = 4;

  UpdateBar();
  UpdateBar(const UpdateBar&) = delete;
  UpdateBar& operator=(const UpdateBar&) = delete;
  ~UpdateBar() override;

  // The share downloaded, 0 to 1: moving there, or there at once without a
  // compositor.
  void SetShare(double share, ::ui::Compositor* compositor);
  // Where the track starts within the view.
  void SetLeading(float leading);
  // A new download starts empty.
  void Reset();

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  ValueMotion share_;
  float leading_ = 0;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_UPDATE_STATUS_H_
