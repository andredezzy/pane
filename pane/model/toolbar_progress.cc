// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/toolbar_progress.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pane::model {

namespace {

// LoadProgressMediator.MINIMUM_LOAD_PROGRESS.
constexpr double kMinimumProgress = 0.05;
// ToolbarProgressBar: ANIMATION_START_THRESHOLD, HIDE_DELAY_MS,
// ALPHA_ANIMATION_DURATION_MS, PROGRESS_FRAME_TIME_CAP_MS.
constexpr base::TimeDelta kSlowModeAfter = base::Seconds(5);
constexpr base::TimeDelta kHideDelay = base::Milliseconds(100);
constexpr base::TimeDelta kFadeDuration = base::Milliseconds(140);
constexpr base::TimeDelta kFrameTimeCap = base::Milliseconds(50);
constexpr double kEpsilon = 1e-6;

// ProgressAnimationSmooth's.
constexpr double kFinishingAcceleration = 7.0;
constexpr double kAcceleration = 0.15;
constexpr double kDeceleration = 0.15;

// ToolbarProgressBarAnimatingView's.
constexpr double kSlowFraction = 0.60;
constexpr double kFastDelayFraction = 0.02;
constexpr double kFastFraction = 0.38;
constexpr base::TimeDelta kShimmerDelay = base::Seconds(1);
constexpr double kSmallBarScale = 0.1;
constexpr double kSmallBarExpansionComplete = 0.6;
constexpr double kMaximumWidth = 400;
constexpr base::TimeDelta kShimmerFadeIn = base::Milliseconds(500);

// The share of `duration` that `elapsed` is, at most all of it. A duration
// shorter than the clock's microsecond is over at once.
double Fraction(base::TimeDelta elapsed, base::TimeDelta duration) {
  return duration.is_positive() ? std::min(elapsed / duration, 1.0) : 1.0;
}

}  // namespace

double UnitBezier::ValueAt(double x) const {
  return SampleY(Solve(std::clamp(x, 0.0, 1.0)));
}

double UnitBezier::SampleX(double t) const {
  return ((ax_ * t + bx_) * t + cx_) * t;
}

double UnitBezier::SampleY(double t) const {
  return ((ay_ * t + by_) * t + cy_) * t;
}

double UnitBezier::SlopeX(double t) const {
  return (3 * ax_ * t + 2 * bx_) * t + cx_;
}

double UnitBezier::Solve(double x) const {
  double t = x;
  for (int i = 0; i < 8; ++i) {
    const double error = SampleX(t) - x;
    if (std::abs(error) < 1e-7) {
      return t;
    }
    const double slope = SlopeX(t);
    if (std::abs(slope) < 1e-6) {
      break;
    }
    t -= error / slope;
  }

  double low = 0;
  double high = 1;
  t = x;
  while (low < high) {
    const double value = SampleX(t);
    if (std::abs(value - x) < 1e-7) {
      return t;
    }
    if (x > value) {
      low = t;
    } else {
      high = t;
    }
    t = (high - low) / 2 + low;
    if (high - low < 1e-9) {
      break;
    }
  }
  return t;
}

Fade::Fade(double value)
    : Fade(value,
           value,
           base::TimeTicks(),
           base::TimeDelta(),
           kLinearOutSlowIn) {}

Fade::Fade(double from,
           double to,
           base::TimeTicks start,
           base::TimeDelta duration,
           const UnitBezier& curve)
    : from_(from), to_(to), start_(start), duration_(duration), curve_(curve) {}

double Fade::ValueAt(base::TimeTicks now) const {
  if (!IsRunning(now)) {
    return to_;
  }
  const double fraction = std::max(now - start_, base::TimeDelta()) / duration_;
  return from_ + (to_ - from_) * curve_.ValueAt(fraction);
}

bool Fade::IsRunning(base::TimeTicks now) const {
  return duration_.is_positive() && now < start_ + duration_;
}

void SmoothAnimation::Reset(double start) {
  progress_ = start;
  velocity_ = 0;
}

double SmoothAnimation::Update(double target,
                               base::TimeDelta frame_time,
                               double resolution) {
  const double seconds = frame_time.InSecondsF();
  const double accelerating = AcceleratingDuration(target, seconds);
  const double decelerating = seconds - accelerating;

  if (accelerating > 0) {
    const double change =
        (target == 1 ? kFinishingAcceleration : kAcceleration) * accelerating;
    progress_ += (velocity_ + 0.5 * change) * accelerating;
    velocity_ += change;
  }

  if (decelerating > 0) {
    const double change = -kDeceleration * decelerating;
    progress_ += (velocity_ + 0.5 * change) * decelerating;
    velocity_ += change;
  }

  progress_ = std::min(progress_, target);

  if (target - progress_ < 0.5 / std::max(resolution, 1.0)) {
    progress_ = target;
    velocity_ = 0;
  }

  return progress_;
}

double SmoothAnimation::AcceleratingDuration(double target,
                                             double frame_time) const {
  if (target == 1) {
    return frame_time;
  }

  const double a = kAcceleration;
  const double d = kDeceleration;
  const double longest =
      -1 / a * velocity_ +
      std::sqrt((2 * d / ((d + a) * a)) * (target - progress_) +
                (d / ((d + a) * a * a)) * velocity_ * velocity_);

  return std::max(0.0, std::min(frame_time, longest));
}

Shimmer::Shimmer() = default;
Shimmer::~Shimmer() = default;

void Shimmer::Start(base::TimeTicks now) {
  if (is_running_) {
    return;
  }

  is_running_ = true;
  UpdateDurations();
  slow_start_ = now;
  slow_ended_ = false;
  last_is_slow_ = true;
  last_fraction_ = 0;
  fade_ = Fade(fade_.ValueAt(now), 1, now, kShimmerFadeIn, kLinearOutSlowIn);
}

void Shimmer::Cancel() {
  if (!is_running_) {
    return;
  }

  is_running_ = false;
  fade_ = Fade(0);
  last_fraction_ = 0;
  progress_width_ = 0;
}

double Shimmer::AlphaAt(base::TimeTicks now) const {
  return fade_.ValueAt(now);
}

void Shimmer::UpdateDurations() {
  if (progress_width_ <= 0) {
    return;
  }

  const double total = std::log(progress_width_) * 0.26;

  if (total <= 0) {
    return;
  }

  slow_duration_ = base::Seconds(total * kSlowFraction);
  fast_delay_ = base::Seconds(total * kFastDelayFraction);
  fast_duration_ = base::Seconds(total * kFastFraction);
}

void Shimmer::Step(base::TimeTicks now) {
  if (!is_running_) {
    return;
  }

  if (!slow_ended_) {
    if (now < slow_start_) {
      return;
    }

    const double fraction = Fraction(now - slow_start_, slow_duration_);
    last_is_slow_ = true;
    last_fraction_ = fraction;

    if (fraction >= 1) {
      slow_ended_ = true;
      // Each part's end takes the width then.
      UpdateDurations();
      fast_start_ = slow_start_ + slow_duration_ + fast_delay_;
    }

    return;
  }

  if (now < fast_start_) {
    return;
  }

  const double fraction = Fraction(now - fast_start_, fast_duration_);
  last_is_slow_ = false;
  last_fraction_ = fraction;

  if (fraction >= 1) {
    // The next pass, a second on.
    UpdateDurations();
    slow_start_ = fast_start_ + fast_duration_ + kShimmerDelay;
    slow_ended_ = false;
  }
}

std::optional<Shimmer::Span> Shimmer::CurrentSpan() const {
  if (!is_running_ || progress_width_ <= 0) {
    return std::nullopt;
  }

  const double fraction = last_fraction_;
  const double interpolated = kFastOutLinearIn.ValueAt(fraction);
  double scale = kBarScale;

  if (last_is_slow_ && fraction <= kSmallBarExpansionComplete) {
    scale = kSmallBarScale + (kBarScale - kSmallBarScale) *
                                 (fraction / kSmallBarExpansionComplete);
  }

  double width = std::min(kMaximumWidth, progress_width_ * scale);
  double center = (progress_width_ + width) * interpolated - width / 2;
  const double right = center + width / 2;
  const double left = center - width / 2;

  if (right > progress_width_) {
    width -= right - progress_width_;
    center -= (right - progress_width_) / 2;
  } else if (left < 0) {
    width -= -left;
    center += -left / 2;
  }

  return Span{.from = center - width / 2, .to = center + width / 2};
}

ToolbarProgress::ToolbarProgress() = default;
ToolbarProgress::~ToolbarProgress() = default;

double ToolbarProgress::AlphaAt(base::TimeTicks now) const {
  return fade_.ValueAt(now);
}

std::optional<ToolbarProgress::ShimmerSpan> ToolbarProgress::ShimmerAt(
    base::TimeTicks now) const {
  const std::optional<Shimmer::Span> span = shimmer_.CurrentSpan();
  if (!span) {
    return std::nullopt;
  }
  return ShimmerSpan{
      .from = span->from, .to = span->to, .alpha = shimmer_.AlphaAt(now)};
}

bool ToolbarProgress::NeedsFrames(base::TimeTicks now) const {
  return smooth_running_ || shimmer_.is_running() || fade_.IsRunning(now);
}

std::optional<base::TimeTicks> ToolbarProgress::NextDeadline() const {
  if (slow_mode_at_ && hide_at_) {
    return std::min(*slow_mode_at_, *hide_at_);
  }
  return slow_mode_at_ ? slow_mode_at_ : hide_at_;
}

void ToolbarProgress::SetWidth(double width) {
  width_ = width;
  if (shimmer_.is_running()) {
    shimmer_.set_progress_width(progress_ * width_);
  }
}

void ToolbarProgress::set_on_change(base::RepeatingClosure on_change) {
  on_change_ = std::move(on_change);
}

void ToolbarProgress::TabChanged(std::optional<double> progress,
                                 base::TimeTicks now) {
  if (progress) {
    StartLoad(now);
    UpdateLoad(*progress, now);
  } else {
    FinishLoad(/*animate=*/false, now);
  }

  NotifyChange();
}

void ToolbarProgress::LoadingChanged(std::optional<double> from,
                                     std::optional<double> to,
                                     base::TimeTicks now) {
  if (!from && !to) {
    return;
  }

  if (!from) {
    // onDidStartNavigationInPrimaryMainFrame.
    StartLoad(now);
    UpdateLoad(*to, now);
  } else if (to) {
    // onLoadProgressChanged: a load still on after the bar finished starts it
    // again.
    if (*to < 1 && completion_ != Completion::kUnfinished) {
      StartLoad(now);
    }
    UpdateLoad(*to, now);
  } else {
    // onLoadStopped: some progress made fills the bar first.
    if (*from > kMinimumProgress && *from < 1) {
      UpdateLoad(1, now);
    }
    FinishLoad(/*animate=*/true, now);
  }

  NotifyChange();
}

void ToolbarProgress::Crashed(base::TimeTicks now) {
  FinishLoad(/*animate=*/false, now);
  NotifyChange();
}

void ToolbarProgress::StartLoad(base::TimeTicks now) {
  if (completion_ == Completion::kUnfinished) {
    return;
  }

  completion_ = Completion::kUnfinished;
  Start(now);
}

void ToolbarProgress::UpdateLoad(double progress, base::TimeTicks now) {
  progress = std::max(progress, kMinimumProgress);

  if (!load_progress_ || std::abs(*load_progress_ - progress) >= kEpsilon) {
    load_progress_ = progress;
    SetProgress(progress, now);
  }

  if (std::abs(progress - 1) < kEpsilon) {
    FinishLoad(/*animate=*/true, now);
  }
}

void ToolbarProgress::FinishLoad(bool animate, base::TimeTicks now) {
  const Completion state = animate ? Completion::kFinishedDoAnimate
                                   : Completion::kFinishedDontAnimate;

  if (state == completion_) {
    return;
  }

  completion_ = state;
  load_progress_.reset();
  Finish(/*fade_out=*/animate, now);
}

void ToolbarProgress::Start(base::TimeTicks now) {
  started_ = true;
  slow_mode_at_ = now + kSlowModeAfter;
  progress_ = 0;
  smooth_.Reset(0);
  AnimateAlpha(1, now);
}

void ToolbarProgress::SetProgress(double value, base::TimeTicks now) {
  if (!started_ || std::abs(target_ - value) < kEpsilon) {
    return;
  }

  target_ = value;
  slow_mode_at_.reset();

  if (!smooth_running_) {
    slow_mode_at_ = now + kSlowModeAfter;
    progress_ = target_;
  }

  if (value >= 1 - kEpsilon) {
    Finish(/*fade_out=*/true, now);
  }
}

void ToolbarProgress::Finish(bool fade_out, base::TimeTicks now) {
  if (std::abs(progress_ - 1) >= kEpsilon) {
    SetProgress(1, now);

    if (smooth_running_ && fade_out) {
      return;
    }
  }

  started_ = false;
  target_ = 0;
  slow_mode_at_.reset();
  shimmer_.Cancel();
  smooth_running_ = false;
  last_frame_.reset();

  if (fade_out) {
    hide_at_ = now + kHideDelay;
  } else {
    hide_at_.reset();
    Hide(/*animated=*/false, now);
  }
}

void ToolbarProgress::Hide(bool animated, base::TimeTicks now) {
  if (started_) {
    return;
  }

  if (animated) {
    AnimateAlpha(0, now);
  } else {
    fade_ = Fade(0);
  }
}

void ToolbarProgress::AnimateAlpha(double value, base::TimeTicks now) {
  const double current = fade_.ValueAt(now);
  const double difference = value - current;

  if (difference == 0) {
    return;
  }

  const base::TimeDelta duration = reduces_motion_
                                       ? base::TimeDelta()
                                       : kFadeDuration * std::abs(difference);
  fade_ = Fade(current, value, now, duration,
               difference > 0 ? kLinearOutSlowIn : kFastOutLinearIn);
}

void ToolbarProgress::Frame(base::TimeTicks now) {
  if (hide_at_ && *hide_at_ <= now) {
    const base::TimeTicks at = *std::exchange(hide_at_, std::nullopt);
    Hide(/*animated=*/true, at);
  }

  if (slow_mode_at_ && *slow_mode_at_ <= now) {
    const base::TimeTicks at = *std::exchange(slow_mode_at_, std::nullopt);
    StartSlowMode(at);
  }

  if (smooth_running_) {
    const base::TimeDelta elapsed =
        std::min(now - last_frame_.value_or(now), kFrameTimeCap);
    last_frame_ = now;

    if (std::abs(progress_ - target_) >= kEpsilon) {
      progress_ =
          std::max(smooth_.Update(target_, elapsed, width_ * scale_), 0.0);
      shimmer_.set_progress_width(progress_ * width_);

      if (std::abs(progress_ - 1) < kEpsilon) {
        Finish(/*fade_out=*/true, now);
      }
    }
  }

  shimmer_.Step(now);
  NotifyChange();
}

void ToolbarProgress::StartSlowMode(base::TimeTicks now) {
  if (!started_) {
    return;
  }

  smooth_.Reset(progress_);
  smooth_running_ = true;
  last_frame_ = now;
  shimmer_.set_progress_width(progress_ * width_);
  shimmer_.Start(now);
}

void ToolbarProgress::NotifyChange() {
  if (on_change_) {
    on_change_.Run();
  }
}

}  // namespace pane::model
