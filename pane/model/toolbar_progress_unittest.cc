// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/toolbar_progress.h"

#include <cmath>
#include <optional>

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's ToolbarProgressTests: the address bar's progress, as
// Chrome's toolbar shows it (ToolbarProgressBar, its LoadProgressMediator,
// ProgressAnimationSmooth, and the shimmer), at times set by hand.

// `seconds` in, to the microsecond.
base::TimeTicks At(double seconds) {
  return base::TimeTicks() + base::Microseconds(std::round(seconds * 1e6));
}

// A bar 300 points wide.
class Bar : public ToolbarProgress {
 public:
  Bar() { SetWidth(300); }
};

// A load shows at once, at least 5%, and fades in over 140 ms.
TEST(ToolbarProgressTest, StartsAtOnce) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.02, At(10));

  EXPECT_TRUE(bar.is_started());
  EXPECT_EQ(bar.progress(), 0.05);
  EXPECT_EQ(bar.AlphaAt(At(10)), 0);
  EXPECT_GT(bar.AlphaAt(At(10.07)), 0.5);
  EXPECT_EQ(bar.AlphaAt(At(10.14)), 1);
}

// Each new value of a load jumps the bar to it.
TEST(ToolbarProgressTest, Jumps) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.1, At(0));
  bar.LoadingChanged(0.1, 0.6, At(0.5));

  EXPECT_EQ(bar.progress(), 0.6);
}

// A finished load fills the bar, holds 100 ms, and fades out over 140 ms.
TEST(ToolbarProgressTest, Finishes) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.1, At(0));
  bar.LoadingChanged(0.1, 0.6, At(1));
  bar.LoadingChanged(0.6, std::nullopt, At(2));

  EXPECT_FALSE(bar.is_started());
  EXPECT_EQ(bar.progress(), 1);
  EXPECT_EQ(bar.NextDeadline(), At(2.1));

  bar.Frame(At(2.05));
  EXPECT_EQ(bar.AlphaAt(At(2.05)), 1);

  bar.Frame(At(2.1));
  EXPECT_GT(bar.AlphaAt(At(2.17)), 0);
  EXPECT_LT(bar.AlphaAt(At(2.17)), 1);
  EXPECT_EQ(bar.AlphaAt(At(2.24)), 0);
}

// A tab come forward with no load hides the bar at once.
TEST(ToolbarProgressTest, HidesForAnIdleTab) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.3, At(0));
  bar.TabChanged(std::nullopt, At(1));

  EXPECT_FALSE(bar.is_started());
  EXPECT_EQ(bar.AlphaAt(At(1)), 0);
  EXPECT_EQ(bar.NextDeadline(), std::nullopt);
}

// A tab come forward mid-load shows its own progress.
TEST(ToolbarProgressTest, ShowsALoadingTab) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.7, At(0));
  bar.TabChanged(0.2, At(1));

  EXPECT_TRUE(bar.is_started());
  EXPECT_EQ(bar.progress(), 0.2);
  EXPECT_EQ(bar.AlphaAt(At(1)), 1);
}

// A crash hides the bar at once.
TEST(ToolbarProgressTest, HidesOnACrash) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.3, At(0));
  bar.Crashed(At(1));

  EXPECT_EQ(bar.AlphaAt(At(1)), 0);
}

// Five seconds with no news start the slow mode: a shimmer, and a fill that
// moves smoothly.
TEST(ToolbarProgressTest, SlowMode) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.3, At(0));
  bar.Frame(At(4.9));
  EXPECT_FALSE(bar.NeedsFrames(At(4.9)));

  bar.Frame(At(5));
  EXPECT_TRUE(bar.NeedsFrames(At(5)));
  EXPECT_TRUE(bar.ShimmerAt(At(5)));

  // News now moves the fill there smoothly, never past it.
  bar.LoadingChanged(0.3, 0.8, At(5.1));
  EXPECT_EQ(bar.progress(), 0.3);

  double time = 5.1;
  double last = bar.progress();

  while (time < 20) {
    time += 1.0 / 60;
    bar.Frame(At(time));
    EXPECT_GE(bar.progress(), last);
    EXPECT_LE(bar.progress(), 0.8);
    last = bar.progress();
  }

  EXPECT_EQ(bar.progress(), 0.8);
}

// In the slow mode, a finish races to full before it fades.
TEST(ToolbarProgressTest, SlowFinish) {
  Bar bar;
  bar.LoadingChanged(std::nullopt, 0.3, At(0));
  bar.Frame(At(5));
  bar.LoadingChanged(0.3, std::nullopt, At(6));

  // Still racing to full, the bar is shown.
  EXPECT_LT(bar.progress(), 1);
  EXPECT_EQ(bar.AlphaAt(At(6)), 1);

  double time = 6;

  while (bar.progress() < 1 && time < 8) {
    time += 1.0 / 60;
    bar.Frame(At(time));
  }

  EXPECT_EQ(bar.progress(), 1);
  EXPECT_LT(time, 6.6);
  EXPECT_FALSE(bar.is_started());
}

// The shimmer stays within the filled part and starts empty.
TEST(ToolbarProgressTest, ShimmerSpan) {
  Shimmer shimmer;
  shimmer.set_progress_width(200);
  shimmer.Start(At(0));
  shimmer.Step(At(0));

  const std::optional<Shimmer::Span> first = shimmer.CurrentSpan();
  ASSERT_TRUE(first);
  EXPECT_EQ(first->to - first->from, 0);

  double time = 0;

  while (time < 4) {
    time += 1.0 / 60;
    shimmer.Step(At(time));

    if (const std::optional<Shimmer::Span> span = shimmer.CurrentSpan()) {
      EXPECT_GE(span->from, -1e-9);
      EXPECT_LE(span->to, 200 + 1e-9);
      EXPECT_LE(span->to - span->from, 200 * Shimmer::kBarScale + 1e-9);
    }
  }
}

// Material's curves, as Android's interpolators draw them.
TEST(ToolbarProgressTest, Curves) {
  EXPECT_EQ(kLinearOutSlowIn.ValueAt(0), 0);
  EXPECT_LT(std::abs(kLinearOutSlowIn.ValueAt(1) - 1), 1e-6);
  EXPECT_GT(kLinearOutSlowIn.ValueAt(0.25), 0.5);
  EXPECT_LT(kFastOutLinearIn.ValueAt(0.25), 0.1);
}

}  // namespace
}  // namespace pane::model
