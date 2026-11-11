// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/time/time.h"
#include "cc/animation/keyframe_model.h"
#include "ui/compositor/callback_layer_animation_observer.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_delegate.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_threaded_animation_delegate.h"
#include "ui/compositor/property_change_reason.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/keyframe/animation_curve.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform_operations.h"
#include "ui/gfx/geometry/transform_util.h"

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

Curve Dot() {
  return EaseInOut(base::Milliseconds(150));
}

Curve Smooth() {
  static const base::NoDestructor<Curve> smooth(Curve::Spring(0.5, 1));
  return *smooth;
}

double ShakeOffset(base::TimeDelta elapsed) {
  // Each step's end, from 0: -8, 8, -6, 6, then 0 again.
  constexpr std::array<double, 5> kSteps = {-8, 8, -6, 6, 0};
  constexpr base::TimeDelta kStep = kShakeDuration / kSteps.size();
  if (elapsed <= base::TimeDelta() || elapsed >= kShakeDuration) {
    return 0;
  }
  static const base::NoDestructor<Curve> step(EaseInOut(kStep));
  const size_t index = static_cast<size_t>(elapsed / kStep);
  const double from = index == 0 ? 0 : kSteps[index - 1];
  const double share = step->Progress(elapsed - kStep * index);
  return from + (kSteps[index] - from) * share;
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

namespace {

// How long one of `compositor`'s frames lasts, nothing when it can't tell.
base::TimeDelta FrameOf(const ::ui::Compositor* compositor) {
  const float rate = compositor ? compositor->refresh_rate() : 0;
  return rate > 0 ? base::Seconds(1) / rate : base::TimeDelta();
}

// Pane's curves for cc: a value from `from` to `to` along `curve`, exactly,
// at any time cc asks for (no samples between which to blend).
class FloatCurve : public gfx::FloatAnimationCurve {
 public:
  FloatCurve(const Curve& curve, float from, float to)
      : curve_(curve), from_(from), to_(to) {}

  // gfx::FloatAnimationCurve:
  base::TimeDelta Duration() const override { return curve_.total(); }
  std::unique_ptr<gfx::AnimationCurve> Clone() const override {
    return std::make_unique<FloatCurve>(curve_, from_, to_);
  }
  float GetValue(base::TimeDelta t) const override {
    return from_ + (to_ - from_) * curve_.Progress(t);
  }
  float GetTransformedValue(
      base::TimeDelta t,
      gfx::TimingFunction::LimitDirection) const override {
    return GetValue(t);
  }

 private:
  const Curve curve_;
  const float from_;
  const float to_;
};

class TransformCurve : public gfx::TransformAnimationCurve {
 public:
  TransformCurve(const Curve& curve,
                 const gfx::Transform& from,
                 const gfx::Transform& to)
      : curve_(curve), from_(from), to_(to) {}

  // gfx::TransformAnimationCurve:
  base::TimeDelta Duration() const override { return curve_.total(); }
  std::unique_ptr<gfx::AnimationCurve> Clone() const override {
    return std::make_unique<TransformCurve>(curve_, from_, to_);
  }
  gfx::TransformOperations GetValue(base::TimeDelta t) const override {
    // As LayerMotion blends one, through its decomposition.
    gfx::TransformOperations operations;
    operations.AppendMatrix(
        gfx::Tween::TransformValueBetween(curve_.Progress(t), from_, to_));
    return operations;
  }
  gfx::TransformOperations GetTransformedValue(
      base::TimeDelta t,
      gfx::TimingFunction::LimitDirection) const override {
    return GetValue(t);
  }
  bool PreservesAxisAlignment() const override {
    return from_.IsScaleOrTranslation() && to_.IsScaleOrTranslation();
  }
  // What cc rasters the layer at while it moves: the larger end's scale.
  bool MaximumScale(float* max_scale) const override {
    const gfx::Vector2dF from =
        gfx::ComputeTransform2dScaleComponents(from_, 0);
    const gfx::Vector2dF to = gfx::ComputeTransform2dScaleComponents(to_, 0);
    *max_scale = std::max({from.x(), from.y(), to.x(), to.y()});
    return *max_scale != 0;
  }

 private:
  const Curve curve_;
  const gfx::Transform from_;
  const gfx::Transform to_;
};

// One property of a layer moving along a curve on cc's animation, through
// the layer's own animator, as Chromium's threaded elements do: the layer
// holds the value it starts from, cc moves it to the target, and once cc is
// done the layer takes the target. cc begins one frame in (a start delay of
// minus a frame), so the first frame shows a frame's progress; the animator
// ends it as cc does, a frame before the curve's whole time from cc's start,
// so the layer never shows the value it holds once cc lets go.
class CurveElement : public ::ui::LayerAnimationElement {
 public:
  CurveElement(AnimatableProperty property,
               const Curve& curve,
               base::TimeDelta frame,
               std::optional<float> opacity,
               std::optional<gfx::Transform> transform)
      : ::ui::LayerAnimationElement(property, curve.total() - frame),
        curve_(curve),
        frame_(frame),
        target_opacity_(opacity.value_or(0)),
        target_transform_(transform.value_or(gfx::Transform())) {}

  CurveElement(const CurveElement&) = delete;
  CurveElement& operator=(const CurveElement&) = delete;
  ~CurveElement() override = default;

  // ui::LayerAnimationElement:
  bool IsThreaded(::ui::LayerAnimationDelegate* delegate) const override {
    return duration().is_positive();
  }

 protected:
  std::string DebugName() const override { return "pane::CurveElement"; }

  void OnStart(::ui::LayerAnimationDelegate* delegate) override {
    from_opacity_ = delegate->GetOpacityForAnimation();
    from_transform_ = delegate->GetTransformForAnimation();
    Set(delegate, from_opacity_, from_transform_);
  }

  void RequestEffectiveStart(::ui::LayerAnimationDelegate* delegate) override {
    if (!IsThreaded(delegate)) {
      set_effective_start_time(requested_start_time());
      return;
    }
    set_effective_start_time(base::TimeTicks());
    // A new id, so a model taken off and one put on in one frame differ.
    UpdateKeyframeModelId();
    std::unique_ptr<gfx::AnimationCurve> curve;
    cc::TargetProperty::Type property;
    if (properties() == OPACITY) {
      curve =
          std::make_unique<FloatCurve>(curve_, from_opacity_, target_opacity_);
      property = cc::TargetProperty::OPACITY;
    } else {
      curve = std::make_unique<TransformCurve>(curve_, from_transform_,
                                               target_transform_);
      property = cc::TargetProperty::TRANSFORM;
    }
    std::unique_ptr<cc::KeyframeModel> model = cc::KeyframeModel::Create(
        std::move(curve), keyframe_model_id(), animation_group_id(),
        cc::KeyframeModel::TargetPropertyId(property));
    model->set_needs_synchronized_start_time(true);
    model->set_start_delay(-frame_);
    delegate->GetThreadedAnimationDelegate()->AddThreadedAnimation(
        std::move(model));
  }

  bool OnProgress(double t, ::ui::LayerAnimationDelegate* delegate) override {
    if (t < 1) {
      return false;
    }
    StopModel(delegate);
    Set(delegate, target_opacity_, target_transform_);
    return true;
  }

  void OnGetTarget(TargetValue* target) const override {
    if (properties() == OPACITY) {
      target->opacity = target_opacity_;
    } else {
      target->transform = target_transform_;
    }
  }

  // Another motion takes over: the layer keeps where cc had it.
  void OnAbort(::ui::LayerAnimationDelegate* delegate) override {
    if (!delegate || !Started()) {
      return;
    }
    StopModel(delegate);
    const base::TimeTicks start = effective_start_time();
    const base::TimeDelta elapsed =
        start.is_null() ? frame_ : base::TimeTicks::Now() - start + frame_;
    const double progress = curve_.Progress(elapsed);
    Set(delegate, from_opacity_ + (target_opacity_ - from_opacity_) * progress,
        gfx::Tween::TransformValueBetween(progress, from_transform_,
                                          target_transform_));
  }

 private:
  void StopModel(::ui::LayerAnimationDelegate* delegate) {
    if (Started() && IsThreaded(delegate)) {
      delegate->GetThreadedAnimationDelegate()->RemoveThreadedAnimation(
          keyframe_model_id());
    }
  }

  void Set(::ui::LayerAnimationDelegate* delegate,
           float opacity,
           const gfx::Transform& transform) {
    if (properties() == OPACITY) {
      delegate->SetOpacityFromAnimation(
          opacity, ::ui::PropertyChangeReason::FROM_ANIMATION);
    } else {
      delegate->SetTransformFromAnimation(
          transform, ::ui::PropertyChangeReason::FROM_ANIMATION);
    }
  }

  const Curve curve_;
  const base::TimeDelta frame_;
  const float target_opacity_;
  const gfx::Transform target_transform_;
  float from_opacity_ = 0;
  gfx::Transform from_transform_;
};

}  // namespace

// static
void CompositorMotion::Start(::ui::Layer* layer,
                             const Curve& curve,
                             const LayerMotion::Target& target,
                             base::OnceClosure done) {
  ::ui::Compositor* compositor = layer->GetCompositor();
  const base::TimeDelta frame = FrameOf(compositor);
  // With less motion asked for, nothing drawing it, or a curve no longer
  // than a frame, the change is at once, still through the animator, so it
  // takes over a motion of the same property as a motion would, and no other.
  const bool at_once =
      !compositor || motion::IsReduced() || curve.total() <= frame;
  ::ui::LayerAnimator* animator = layer->GetAnimator();
  animator->set_preemption_strategy(
      ::ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  // Runs `done` once every property has reached its target, never when one
  // is taken over; deletes itself.
  auto* observer = new ::ui::CallbackLayerAnimationObserver(base::BindRepeating(
      [](base::OnceClosure* done,
         const ::ui::CallbackLayerAnimationObserver& observer) {
        if (observer.aborted_count() == 0 && *done) {
          std::move(*done).Run();
        }
        return true;
      },
      base::Owned(std::make_unique<base::OnceClosure>(std::move(done)))));
  std::vector<::ui::LayerAnimationSequence*> sequences;
  if (target.opacity) {
    sequences.push_back(new ::ui::LayerAnimationSequence(
        at_once ? ::ui::LayerAnimationElement::CreateOpacityElement(
                      *target.opacity, base::TimeDelta())
                : std::make_unique<CurveElement>(
                      ::ui::LayerAnimationElement::OPACITY, curve, frame,
                      target.opacity, std::nullopt)));
  }
  if (target.transform) {
    sequences.push_back(new ::ui::LayerAnimationSequence(
        at_once ? ::ui::LayerAnimationElement::CreateTransformElement(
                      *target.transform, base::TimeDelta())
                : std::make_unique<CurveElement>(
                      ::ui::LayerAnimationElement::TRANSFORM, curve, frame,
                      std::nullopt, target.transform)));
  }
  for (::ui::LayerAnimationSequence* sequence : sequences) {
    sequence->AddObserver(observer);
  }
  animator->StartTogether(sequences);
  // Only now may it run `done` and go: a sequence that ends at once ends
  // inside StartTogether.
  observer->SetActive();
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
