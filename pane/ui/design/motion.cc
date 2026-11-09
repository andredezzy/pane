// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/motion.h"

#include <cmath>
#include <numbers>
#include <utility>

#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"

namespace pane {

namespace {

// Where a spring stands `t` seconds into a change from 0 to 1, at rest at
// first: SwiftUI's spring, its natural frequency 2π over the response.
double SpringAt(double response, double damping, double t) {
  const double omega = 2 * std::numbers::pi / response;
  if (damping < 1) {
    const double damped = omega * std::sqrt(1 - damping * damping);
    return 1 - std::exp(-damping * omega * t) *
                   (std::cos(damped * t) +
                    damping * omega / damped * std::sin(damped * t));
  }
  if (damping == 1) {
    return 1 - std::exp(-omega * t) * (1 + omega * t);
  }
  const double root = std::sqrt(damping * damping - 1);
  const double r1 = -omega * (damping - root);
  const double r2 = -omega * (damping + root);
  return 1 - (r2 * std::exp(r1 * t) - r1 * std::exp(r2 * t)) / (r2 - r1);
}

// When a spring has settled: within a thousandth of its target, from then
// on, as SwiftUI ends one.
base::TimeDelta SpringDuration(double response, double damping) {
  constexpr double kStep = 0.001;
  double settled = 0;
  for (double t = 0; t < 10; t += kStep) {
    if (std::abs(1 - SpringAt(response, damping, t)) >= 0.001) {
      settled = t + kStep;
    }
  }
  return base::Seconds(settled);
}

}  // namespace

Curve::Curve(std::optional<gfx::CubicBezier> bezier,
             double response,
             double damping,
             base::TimeDelta duration,
             base::TimeDelta delay)
    : bezier_(std::move(bezier)),
      response_(response),
      damping_(damping),
      duration_(duration),
      delay_(delay) {}

Curve::Curve(const Curve&) = default;
Curve::~Curve() = default;

// static
Curve Curve::Cubic(double x1,
                   double y1,
                   double x2,
                   double y2,
                   base::TimeDelta duration,
                   base::TimeDelta delay) {
  return Curve(gfx::CubicBezier(x1, y1, x2, y2), 0, 1, duration, delay);
}

// static
Curve Curve::Spring(double response, double damping, base::TimeDelta delay) {
  return Curve(std::nullopt, response, damping,
               SpringDuration(response, damping), delay);
}

double Curve::Progress(base::TimeDelta elapsed) const {
  const base::TimeDelta into = elapsed - delay_;
  if (into <= base::TimeDelta()) {
    return 0;
  }
  if (into >= duration_) {
    return 1;
  }
  if (bezier_) {
    return bezier_->Solve(into / duration_);
  }
  return SpringAt(response_, damping_, into.InSecondsF());
}

namespace motion {

namespace {

// SwiftUI's named curves, which are CSS's.
Curve EaseOut(base::TimeDelta duration,
              base::TimeDelta delay = base::TimeDelta()) {
  return Curve::Cubic(0, 0, 0.58, 1, duration, delay);
}

Curve EaseIn(base::TimeDelta duration) {
  return Curve::Cubic(0.42, 0, 1, 1, duration);
}

Curve EaseInOut(base::TimeDelta duration,
                base::TimeDelta delay = base::TimeDelta()) {
  return Curve::Cubic(0.42, 0, 0.58, 1, duration, delay);
}

constexpr base::TimeDelta kBarExit = base::Milliseconds(120);

}  // namespace

Curve Hover() {
  return EaseOut(base::Milliseconds(150));
}

Curve HoverTurn() {
  return EaseOut(base::Milliseconds(150), base::Milliseconds(80));
}

Curve Toggle() {
  return EaseOut(base::Milliseconds(150));
}

Curve Morph() {
  return Curve::Spring(0.32, 0.86);
}

Curve Slide() {
  return Curve::Spring(0.28, 0.9);
}

Curve Reveal() {
  return Curve::Spring(0.3, 1);
}

Curve Drop() {
  return EaseOut(base::Milliseconds(120));
}

Curve Panel() {
  return EaseOut(base::Milliseconds(150));
}

Curve Sheet() {
  return EaseOut(base::Milliseconds(200));
}

Curve LockIn() {
  return Curve::Cubic(0.16, 1, 0.3, 1, base::Milliseconds(400));
}

Curve LockOut() {
  return EaseIn(base::Milliseconds(350));
}

Curve Fade() {
  return EaseOut(base::Milliseconds(300));
}

Curve Status() {
  return EaseInOut(base::Milliseconds(200));
}

Curve StatusAfterBar() {
  return EaseInOut(base::Milliseconds(200), kBarExit);
}

Curve BarExit() {
  return EaseOut(kBarExit);
}

Curve Progress() {
  return Curve::Cubic(0, 0, 1, 1, base::Milliseconds(200));
}

bool IsReduced() {
  return gfx::Animation::PrefersReducedMotion();
}

}  // namespace motion

// static
void LayerMotion::Start(::ui::Layer* layer,
                        const Curve& curve,
                        const Target& target,
                        base::OnceClosure done) {
  // With less motion asked for, or nothing drawing it, the change is at once.
  if (!layer->GetCompositor() || motion::IsReduced()) {
    if (target.opacity) {
      layer->SetOpacity(*target.opacity);
    }
    if (target.transform) {
      layer->SetTransform(*target.transform);
    }
    if (done) {
      std::move(done).Run();
    }
    return;
  }
  new LayerMotion(layer, curve, target, std::move(done));
}

LayerMotion::LayerMotion(::ui::Layer* layer,
                         const Curve& curve,
                         const Target& target,
                         base::OnceClosure done)
    : layer_(layer),
      compositor_(layer->GetCompositor()),
      curve_(curve),
      from_opacity_(layer->opacity()),
      from_transform_(layer->transform()),
      target_(target),
      done_(std::move(done)) {
  layer_observation_.Observe(layer);
  compositor_->AddAnimationObserver(this);
}

LayerMotion::~LayerMotion() = default;

void LayerMotion::OnAnimationStep(base::TimeTicks timestamp) {
  if (!start_) {
    start_ = timestamp;
  }
  const base::TimeDelta elapsed = timestamp - *start_;
  if (elapsed >= curve_.total()) {
    Apply(1);
    End();
    return;
  }
  Apply(curve_.Progress(elapsed));
}

void LayerMotion::OnCompositingShuttingDown(::ui::Compositor* compositor) {
  Apply(1);
  End();
}

void LayerMotion::LayerDestroyed(::ui::Layer* layer) {
  layer_ = nullptr;
  End();
}

void LayerMotion::Apply(double progress) {
  if (!layer_) {
    return;
  }
  if (target_.opacity) {
    layer_->SetOpacity(from_opacity_ +
                       (*target_.opacity - from_opacity_) * progress);
  }
  if (target_.transform) {
    layer_->SetTransform(gfx::Tween::TransformValueBetween(
        progress, from_transform_, *target_.transform));
  }
}

void LayerMotion::End() {
  if (compositor_ && compositor_->HasAnimationObserver(this)) {
    compositor_->RemoveAnimationObserver(this);
  }
  layer_observation_.Reset();
  base::OnceClosure done = std::move(done_);
  delete this;
  if (done) {
    std::move(done).Run();
  }
}

ValueMotion::ValueMotion(double value,
                         base::RepeatingCallback<void(double)> step)
    : value_(value), step_(std::move(step)) {}

ValueMotion::~ValueMotion() {
  Stop();
}

void ValueMotion::MoveTo(double target,
                         const Curve& curve,
                         ::ui::Compositor* compositor) {
  Stop();
  if (!compositor || motion::IsReduced()) {
    Set(target);
    return;
  }
  curve_.emplace(curve);
  from_ = value_;
  to_ = target;
  start_.reset();
  compositor_ = compositor;
  compositor_->AddAnimationObserver(this);
}

void ValueMotion::OnAnimationStep(base::TimeTicks timestamp) {
  if (!start_) {
    start_ = timestamp;
  }
  const base::TimeDelta elapsed = timestamp - *start_;
  if (elapsed >= curve_->total()) {
    const double to = to_;
    Stop();
    Set(to);
    return;
  }
  Set(from_ + (to_ - from_) * curve_->Progress(elapsed));
}

void ValueMotion::OnCompositingShuttingDown(::ui::Compositor* compositor) {
  // The window is closing: the value lands, with nothing left to lay out.
  Stop();
  value_ = to_;
}

void ValueMotion::Set(double value) {
  value_ = value;
  step_.Run(value);
}

void ValueMotion::Stop() {
  if (compositor_ && compositor_->HasAnimationObserver(this)) {
    compositor_->RemoveAnimationObserver(this);
  }
  compositor_ = nullptr;
  curve_.reset();
}

}  // namespace pane
