// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_TOOLBAR_PROGRESS_H_
#define PANE_MODEL_TOOLBAR_PROGRESS_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/time/time.h"

namespace pane::model {

// A cubic Bézier timing curve, as CSS and Android's PathInterpolator take it.
class UnitBezier {
 public:
  constexpr UnitBezier(double x1, double y1, double x2, double y2)
      : cx_(3 * x1),
        bx_(3 * (x2 - x1) - cx_),
        ax_(1 - cx_ - bx_),
        cy_(3 * y1),
        by_(3 * (y2 - y1) - cy_),
        ay_(1 - cy_ - by_) {}

  double ValueAt(double x) const;

 private:
  double SampleX(double t) const;
  double SampleY(double t) const;
  double SlopeX(double t) const;
  // The curve's parameter at `x`: Newton's method, then halving.
  double Solve(double x) const;

  double cx_;
  double bx_;
  double ax_;
  double cy_;
  double by_;
  double ay_;
};

// Material's curves: Android's Interpolators.LINEAR_OUT_SLOW_IN and
// FAST_OUT_LINEAR_IN.
inline constexpr UnitBezier kLinearOutSlowIn(0, 0, 0.2, 1);
inline constexpr UnitBezier kFastOutLinearIn(0.4, 0, 1, 1);

// An opacity animating from one value to another, or holding one.
class Fade {
 public:
  explicit Fade(double value);
  Fade(double from,
       double to,
       base::TimeTicks start,
       base::TimeDelta duration,
       const UnitBezier& curve);

  double ValueAt(base::TimeTicks now) const;
  bool IsRunning(base::TimeTicks now) const;

 private:
  double from_;
  double to_;
  base::TimeTicks start_;
  base::TimeDelta duration_;
  UnitBezier curve_;
};

// ProgressAnimationSmooth: a constant acceleration, then a constant
// deceleration, to stop at the target; a finish (target 1) accelerates hard.
class SmoothAnimation {
 public:
  void Reset(double start);
  // The progress `frame_time` on, at rest on the target once within half a
  // pixel of it, `resolution` pixels spanning the bar.
  double Update(double target, base::TimeDelta frame_time, double resolution);

 private:
  // How long it accelerates within the frame, to come to rest at the target:
  // v0 + A dA - D dD = 0, and the distance covered the gap.
  double AcceleratingDuration(double target, double frame_time) const;

  double progress_ = 0;
  double velocity_ = 0;
};

// ToolbarProgressBarAnimatingView: a block crossing the filled part, slowly
// then fast, a second apart. It grows from 10% to 30% of the filled width over
// the first 60% of the slow pass, at most 400 points, and fades in over 500
// ms. A pass lasts ln(width) × 260 ms: 60% slow, 2% pause, 38% fast.
class Shimmer {
 public:
  static constexpr double kBarScale = 0.3;

  // Points from the bar's leading edge.
  struct Span {
    double from = 0;
    double to = 0;
  };

  Shimmer();
  ~Shimmer();

  bool is_running() const { return is_running_; }
  // The filled part's width, in points.
  void set_progress_width(double width) { progress_width_ = width; }

  void Start(base::TimeTicks now);
  void Cancel();
  double AlphaAt(base::TimeTicks now) const;
  void Step(base::TimeTicks now);
  // Where the block is (updateAnimation), clipped to the filled part; nullopt
  // while it does not run.
  std::optional<Span> CurrentSpan() const;

 private:
  // The pass's durations for the filled width now (updateAnimationDuration).
  void UpdateDurations();

  double progress_width_ = 0;
  bool is_running_ = false;
  Fade fade_{0};
  // The pass under way: when its slow part starts and how long each part is.
  base::TimeTicks slow_start_;
  base::TimeDelta slow_duration_ = base::Milliseconds(300);
  base::TimeDelta fast_delay_;
  base::TimeDelta fast_duration_ = base::Milliseconds(300);
  base::TimeTicks fast_start_;
  bool slow_ended_ = false;
  // The last animation updated, and its fraction (mLastUpdatedAnimation).
  bool last_is_slow_ = true;
  double last_fraction_ = 0;
};

// The address bar's load progress as Chrome's own toolbar shows it: a port of
// Chromium's ToolbarProgressBar (chrome/browser/ui/android/toolbar) as stable
// Chrome runs it, its composited animation off by default, with the rules of
// its LoadProgressMediator for what moves it, its ProgressAnimationSmooth, and
// its ToolbarProgressBarAnimatingView (the user's call: exactly as a stable
// browser does it).
//
// A load shows its real progress, at least 5%, the bar jumping to each new
// value; it fades in over 140 ms. Five seconds with no news start its slow
// mode: a shimmer crosses the filled part and the fill moves smoothly, on a
// constant acceleration and deceleration. A finished load fills the bar,
// holds 100 ms, and fades out over 140 ms; a tab come forward with no load,
// or a crash, hides it at once. Time comes in (Frame()), and the drawing goes
// out: the share filled, the opacity, and the shimmer's span.
class ToolbarProgress {
 public:
  // The shimmer's span in points from the bar's leading edge, and its
  // opacity.
  struct ShimmerSpan {
    double from = 0;
    double to = 0;
    double alpha = 0;
  };

  ToolbarProgress();
  ToolbarProgress(const ToolbarProgress&) = delete;
  ToolbarProgress& operator=(const ToolbarProgress&) = delete;
  ~ToolbarProgress();

  // What to draw.
  double progress() const { return progress_; }
  bool is_started() const { return started_; }
  double AlphaAt(base::TimeTicks now) const;
  // nullopt while the shimmer does not run.
  std::optional<ShimmerSpan> ShimmerAt(base::TimeTicks now) const;

  // Whether a frame changes anything: a fade, the slow mode.
  bool NeedsFrames(base::TimeTicks now) const;
  // The next moment something is due with no frame asking: the slow mode, or
  // the hide after a finish.
  std::optional<base::TimeTicks> NextDeadline() const;

  // The bar's width in points, and the screen's pixels per point: the smooth
  // fill rounds to a pixel, and the shimmer spans the filled part.
  void SetWidth(double width);
  void set_scale(double scale) { scale_ = scale; }
  // While the system reduces motion, the fades are instant.
  void set_reduces_motion(bool reduces_motion) {
    reduces_motion_ = reduces_motion;
  }
  // Called after each change of what to draw.
  void set_on_change(base::RepeatingClosure on_change);

  // What moves it (LoadProgressMediator).

  // Another tab forward: its load as it stands, the bar hidden at once when
  // it has none (onNewTabObserved).
  void TabChanged(std::optional<double> progress, base::TimeTicks now);
  // The selected tab's progress, nullopt while it does not load a new
  // document.
  void LoadingChanged(std::optional<double> from,
                      std::optional<double> to,
                      base::TimeTicks now);
  // onCrash.
  void Crashed(base::TimeTicks now);

  // The deadlines due, then the slow mode's step.
  void Frame(base::TimeTicks now);

 private:
  // LoadProgressProperties.CompletionState.
  enum class Completion {
    kUnfinished,
    kFinishedDoAnimate,
    kFinishedDontAnimate,
  };

  // The mediator's model notifies on a change of value alone. Its progress
  // starts again with each load here: Chrome keeps it, so a load starting at
  // the value the last one ended at drew an empty bar until its next.
  void StartLoad(base::TimeTicks now);
  void UpdateLoad(double progress, base::TimeTicks now);
  void FinishLoad(bool animate, base::TimeTicks now);

  // The bar (ToolbarProgressBar).
  void Start(base::TimeTicks now);
  void SetProgress(double value, base::TimeTicks now);
  void Finish(bool fade_out, base::TimeTicks now);
  void Hide(bool animated, base::TimeTicks now);
  // LINEAR_OUT_SLOW_IN in, FAST_OUT_LINEAR_IN out, for as long as the share
  // of the way left of 140 ms.
  void AnimateAlpha(double value, base::TimeTicks now);
  // mStartSmoothIndeterminate.
  void StartSlowMode(base::TimeTicks now);
  void NotifyChange();

  double progress_ = 0;
  double width_ = 0;
  double scale_ = 2;
  bool reduces_motion_ = false;
  base::RepeatingClosure on_change_;

  bool started_ = false;
  double target_ = 0;
  Completion completion_ = Completion::kFinishedDontAnimate;
  // The mediator's last progress (LoadProgressProperties.PROGRESS).
  std::optional<double> load_progress_;
  SmoothAnimation smooth_;
  bool smooth_running_ = false;
  std::optional<base::TimeTicks> last_frame_;
  std::optional<base::TimeTicks> slow_mode_at_;
  std::optional<base::TimeTicks> hide_at_;
  Fade fade_{0};
  Shimmer shimmer_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_TOOLBAR_PROGRESS_H_
