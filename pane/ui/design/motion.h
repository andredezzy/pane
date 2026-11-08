// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_MOTION_H_
#define PANE_UI_DESIGN_MOTION_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/compositor/layer_observer.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/gfx/geometry/transform.h"

namespace ui {
class Layer;
}

namespace pane {

// How a change moves (DESIGN.md › Motion): a cubic Bézier over a duration,
// or a spring by its response and damping, as SwiftUI defines them, each
// after a delay. Every animation in Pane is one of these, by what it is for.
class Curve {
 public:
  static Curve Cubic(double x1,
                     double y1,
                     double x2,
                     double y2,
                     base::TimeDelta duration,
                     base::TimeDelta delay = base::TimeDelta());
  // `response` is the period of the undamped spring, `damping` its fraction
  // of critical damping (1: no overshoot).
  static Curve Spring(double response,
                      double damping,
                      base::TimeDelta delay = base::TimeDelta());

  Curve(const Curve&);
  ~Curve();

  // How far along the change is, `elapsed` after it began: 0 to 1, a spring
  // past 1 as it overshoots.
  double Progress(base::TimeDelta elapsed) const;
  // When it has settled.
  base::TimeDelta total() const { return delay_ + duration_; }

 private:
  Curve(std::optional<gfx::CubicBezier> bezier,
        double response,
        double damping,
        base::TimeDelta duration,
        base::TimeDelta delay);

  std::optional<gfx::CubicBezier> bezier_;
  double response_ = 0;
  double damping_ = 1;
  base::TimeDelta duration_;
  base::TimeDelta delay_;
};

// The curves Pane uses, from the Swift app's Motion table.
namespace motion {
// Hover fills and presses: 0.15 s, easing out.
Curve Hover();
// The profile sheet's scrim and slide: 0.2 s, easing out.
Curve Sheet();
// An indicator's own change of shape: a spring of 0.32 s, damped 0.86.
Curve Morph();
// Whether the system asks for less motion: every change is then at once.
bool IsReduced();
}  // namespace motion

// Moves a layer's opacity and transform along a curve, a step on each of
// its compositor's frames, never repainting what it moves; ends at the
// target, then runs `done`. The layer going ends it there.
class LayerMotion : public ::ui::CompositorAnimationObserver,
                    public ::ui::LayerObserver {
 public:
  struct Target {
    std::optional<float> opacity;
    std::optional<gfx::Transform> transform;
  };

  // Starts at once from the layer's current values; deletes itself when it
  // ends.
  static void Start(::ui::Layer* layer,
                    const Curve& curve,
                    const Target& target,
                    base::OnceClosure done = base::OnceClosure());

  LayerMotion(const LayerMotion&) = delete;
  LayerMotion& operator=(const LayerMotion&) = delete;
  ~LayerMotion() override;

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(::ui::Compositor* compositor) override;

  // ui::LayerObserver:
  void LayerDestroyed(::ui::Layer* layer) override;

 private:
  LayerMotion(::ui::Layer* layer,
              const Curve& curve,
              const Target& target,
              base::OnceClosure done);

  void Apply(double progress);
  void End();

  raw_ptr<::ui::Layer> layer_;
  raw_ptr<::ui::Compositor> compositor_;
  const Curve curve_;
  const float from_opacity_;
  const gfx::Transform from_transform_;
  const Target target_;
  base::OnceClosure done_;
  std::optional<base::TimeTicks> start_;
  base::ScopedObservation<::ui::Layer, ::ui::LayerObserver> layer_observation_{
      this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_MOTION_H_
