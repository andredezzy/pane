// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/motion.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "base/no_destructor.h"
#include "base/time/time.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"

namespace pane {

namespace {

// A spring's natural frequency: SwiftUI's, 2π over the response.
double Omega(double response) {
  return 2 * std::numbers::pi / response;
}

// How far a spring stands from its target `t` seconds into a change that
// began `offset` from it, moving at `velocity` per second, and how fast it
// moves then: the damped oscillator SwiftUI's springs are.
Curve::Point SpringState(double response,
                         double damping,
                         double offset,
                         double velocity,
                         double t) {
  const double omega = Omega(response);
  if (damping < 1) {
    const double damped = omega * std::sqrt(1 - damping * damping);
    const double decay = damping * omega;
    const double sine = (velocity + decay * offset) / damped;
    const double envelope = std::exp(-decay * t);
    const double c = std::cos(damped * t);
    const double s = std::sin(damped * t);
    return {envelope * (offset * c + sine * s),
            envelope * ((sine * damped - decay * offset) * c -
                        (offset * damped + decay * sine) * s)};
  }
  if (damping == 1) {
    const double rate = velocity + omega * offset;
    const double envelope = std::exp(-omega * t);
    return {envelope * (offset + rate * t),
            envelope * (rate - omega * (offset + rate * t))};
  }
  const double root = std::sqrt(damping * damping - 1);
  const double r1 = -omega * (damping - root);
  const double r2 = -omega * (damping + root);
  const double c1 = (velocity - r2 * offset) / (r1 - r2);
  const double c2 = offset - c1;
  return {c1 * std::exp(r1 * t) + c2 * std::exp(r2 * t),
          c1 * r1 * std::exp(r1 * t) + c2 * r2 * std::exp(r2 * t)};
}

// Where a spring stands `t` seconds into a change from 0 to 1, at rest at
// first.
double SpringAt(double response, double damping, double t) {
  return 1 + SpringState(response, damping, -1, 0, t).value;
}

// The farthest a spring standing `offset` from its target, moving at
// `velocity`, will stand from it from then on, or more: an underdamped one's
// envelope, and for the others a bound on theirs.
double SpringReach(double response,
                   double damping,
                   double offset,
                   double velocity) {
  const double omega = Omega(response);
  if (damping < 1) {
    const double damped = omega * std::sqrt(1 - damping * damping);
    return std::hypot(offset, (velocity + damping * omega * offset) / damped);
  }
  return 2 * std::abs(offset) + std::abs(velocity) / omega;
}

// When a spring has settled: within a thousandth of its target, from then
// on, as SwiftUI ends one. Read a millisecond at a time until it can't leave
// that thousandth again.
base::TimeDelta SpringDuration(double response, double damping) {
  constexpr double kStep = 0.001;
  double settled = 0;
  for (double t = 0; t < 10; t += kStep) {
    const Curve::Point state = SpringState(response, damping, -1, 0, t);
    if (std::abs(state.value) >= 0.001) {
      settled = t + kStep;
    }
    if (SpringReach(response, damping, state.value, state.velocity) < 0.001) {
      break;
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

Curve::Point Curve::At(double from,
                       double to,
                       double velocity,
                       base::TimeDelta elapsed) const {
  const base::TimeDelta into = elapsed - delay_;
  // Waiting, nothing moves; a spring begins at the speed it took on.
  if (into < base::TimeDelta() || (into.is_zero() && bezier_)) {
    return {from, 0};
  }
  // Settled from rest, as Progress has it.
  if ((bezier_ || velocity == 0) && into >= duration_) {
    return {to, 0};
  }
  if (bezier_) {
    const double x = into / duration_;
    return {from + (to - from) * bezier_->Solve(x),
            (to - from) * bezier_->Slope(x) / duration_.InSecondsF()};
  }
  const Point state =
      SpringState(response_, damping_, from - to, velocity, into.InSecondsF());
  return {to + state.value, state.velocity};
}

bool Curve::Settled(double from,
                    double to,
                    double velocity,
                    base::TimeDelta elapsed) const {
  if (bezier_ || velocity == 0) {
    return elapsed >= total();
  }
  if (elapsed <= delay_) {
    return false;
  }
  if (elapsed - delay_ >= base::Seconds(10)) {
    return true;
  }
  // Never again farther from its target than a thousandth of the change, or
  // of how far its speed alone would carry it.
  const double near = 0.001 * std::max(std::abs(from - to),
                                       std::abs(velocity) / Omega(response_));
  const Point now = At(from, to, velocity, elapsed);
  return SpringReach(response_, damping_, now.value - to, now.velocity) <= near;
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

// The springs, each worked out once: a drag's every move can ask for one.
Curve Morph() {
  static const base::NoDestructor<Curve> morph(Curve::Spring(0.32, 0.86));
  return *morph;
}

Curve Slide() {
  static const base::NoDestructor<Curve> slide(Curve::Spring(0.28, 0.9));
  return *slide;
}

Curve Reveal() {
  static const base::NoDestructor<Curve> reveal(Curve::Spring(0.3, 1));
  return *reveal;
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

Curve Pulse() {
  return EaseInOut(base::Seconds(1));
}

bool IsReduced() {
  return gfx::Animation::PrefersReducedMotion();
}

}  // namespace motion

namespace {

// Where a motion's clock starts, at its first frame: one frame back, so the
// frame shows one frame's progress, as Core Animation's first frame of a
// change did, and a change answers its input in the next frame. Started at
// the frame itself, the first frame showed the start again, nothing to see,
// and the change came a frame later.
base::TimeTicks FirstFrameStart(base::TimeTicks timestamp,
                                const ::ui::Compositor* compositor) {
  const float rate = compositor ? compositor->refresh_rate() : 0;
  return rate > 0 ? timestamp - base::Seconds(1) / rate : timestamp;
}

}  // namespace

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
    start_ = FirstFrameStart(timestamp, compositor_);
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
  // A spring takes on the speed of the change under way, as SwiftUI merges
  // one into it.
  const double velocity = curve_ && curve.is_spring() ? speed_ : 0;
  Stop();
  if (!compositor || motion::IsReduced()) {
    Set(target);
    return;
  }
  curve_.emplace(curve);
  from_ = value_;
  to_ = target;
  velocity_ = velocity;
  speed_ = velocity;
  start_.reset();
  compositor_ = compositor;
  compositor_->AddAnimationObserver(this);
}

void ValueMotion::OnAnimationStep(base::TimeTicks timestamp) {
  if (!start_) {
    start_ = FirstFrameStart(timestamp, compositor_);
  }
  const base::TimeDelta elapsed = timestamp - *start_;
  if (curve_->Settled(from_, to_, velocity_, elapsed)) {
    const double to = to_;
    Stop();
    Set(to);
    return;
  }
  const Curve::Point point = curve_->At(from_, to_, velocity_, elapsed);
  speed_ = point.velocity;
  Set(point.value);
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
  speed_ = 0;
}

}  // namespace pane
