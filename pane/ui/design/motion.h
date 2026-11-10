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

  // Where a change from `from` to `to` stands `elapsed` after it began, and
  // how fast it moves then, per second. A spring may begin already moving at
  // `velocity`, as SwiftUI merges a spring into a change under way, which
  // keeps its speed; a cubic begins at rest.
  struct Point {
    double value = 0;
    double velocity = 0;
  };
  Point At(double from,
           double to,
           double velocity,
           base::TimeDelta elapsed) const;
  // Whether that change has settled by `elapsed`: a cubic at its end, a
  // spring from rest when `total()` has passed, and one that began moving
  // once it stays within a thousandth of the change (or of how far its speed
  // would carry it) and has all but stopped.
  bool Settled(double from,
               double to,
               double velocity,
               base::TimeDelta elapsed) const;

  bool is_spring() const { return !bezier_.has_value(); }
  // How long it waits, then how long it moves, and when it has settled.
  base::TimeDelta delay() const { return delay_; }
  base::TimeDelta duration() const { return duration_; }
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

// Every curve Pane moves by, by what it is for: the Swift app's Motion table,
// all 16 (DESIGN.md › Motion), and the update dot's pulse, which the Swift
// passed through Motion.unlessReduced. SwiftUI's easeOut, easeIn, and
// easeInOut are CSS's (0, 0, 0.58, 1), (0.42, 0, 1, 1), and (0.42, 0, 0.58,
// 1). A driver takes none while the system asks for less motion (IsReduced):
// every change is then at once, so a view honors the setting with no check of
// its own.
namespace motion {
// Hover fills, the tints that change with them, and presses: 0.15 s, easing
// out.
Curve Hover();
// A hover change that waits its turn, so neither of two things sits over the
// other (the update pill's dismiss button and its bar): the hover's, after
// 0.08 s.
Curve HoverTurn();
// A switch's thumb crossing over, and what its row shows or hides: as quick
// as a hover.
Curve Toggle();
// An indicator's own change of shape (the active profile's bar growing into
// its square): a spring of 0.32 s, damped 0.86, with no overshoot to speak
// of.
Curve Morph();
// Rows sliding aside for a dragged one, and its copy settling into place: a
// spring of 0.28 s, damped 0.9.
Curve Slide();
// The sidebar scrolling to a tab chosen elsewhere that was out of its view: a
// spring of 0.3 s, critically damped.
Curve Reveal();
// The copy of a dragged row going, once it has settled: 0.12 s, easing out.
Curve Drop();
// A dialog over the window appearing and going (Quit Pane?, a page's
// dialog): 0.15 s, easing out.
Curve Panel();
// The profile sheet sliding in from the right as its scrim fades in, and
// back out: 0.2 s, easing out.
Curve Sheet();
// The PIN screen rising, slowing hard as it lands (0.4 s), and sinking back
// (0.35 s, easing in).
Curve LockIn();
Curve LockOut();
// A quiet element coming and going (the update pill, the full screen hint):
// 0.3 s, easing out.
Curve Fade();
// A status changing in place, the update's text crossfading: 0.2 s, easing
// in and out.
Curve Status();
// A status taking the place of a bar going beside it: the same crossfade,
// once the bar has gone (BarExit's 0.12 s).
Curve StatusAfterBar();
// A progress bar going once its work is done: 0.12 s, easing out.
Curve BarExit();
// A progress bar moving to its new share: 0.2 s, linear.
Curve Progress();
// A dot breathing while its work runs (the update's, Tailwind's
// animate-pulse): to half and back, each way 1 s, easing in and out.
Curve Pulse();
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

// Moves a value toward a target along a curve, a step on each of a
// compositor's frames, for what a layer's opacity and transform can't carry
// (a width, a corner's radius, where a sliding row stands): `step` lays out,
// paints, or moves a layer at each value. Its view owns it; a new target
// starts from the value reached, so a change of mind mid-way doesn't jump,
// and a spring keeps the speed it had, as SwiftUI's does, so it doesn't stop
// dead either.
class ValueMotion : public ::ui::CompositorAnimationObserver {
 public:
  ValueMotion(double value, base::RepeatingCallback<void(double)> step);
  ValueMotion(const ValueMotion&) = delete;
  ValueMotion& operator=(const ValueMotion&) = delete;
  ~ValueMotion() override;

  // At once when `compositor` is null or less motion is asked for.
  void MoveTo(double target, const Curve& curve, ::ui::Compositor* compositor);
  double value() const { return value_; }
  // Where it is heading, or where it rests.
  double target() const { return curve_ ? to_ : value_; }
  // Whether it is on its way.
  bool is_moving() const { return curve_.has_value(); }

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(::ui::Compositor* compositor) override;

 private:
  void Set(double value);
  void Stop();

  double value_;
  base::RepeatingCallback<void(double)> step_;
  std::optional<Curve> curve_;
  double from_ = 0;
  double to_ = 0;
  // The speed it began with, and the one at the value reached, per second.
  double velocity_ = 0;
  double speed_ = 0;
  std::optional<base::TimeTicks> start_;
  raw_ptr<::ui::Compositor> compositor_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_MOTION_H_
