// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/motion.h"

#include <string>
#include <vector>

#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/animation/animation.h"

namespace pane {

namespace {

using base::Milliseconds;

struct Named {
  std::string name;
  Curve curve;
  base::TimeDelta delay;
  base::TimeDelta duration;
};

// The Swift app's Motion table, as Motion.swift writes each one: a cubic's
// duration and delay; a spring's settling time comes from its response and
// damping (below).
std::vector<Named> Table() {
  return {
      {"hover", motion::Hover(), {}, Milliseconds(150)},
      {"hoverTurn", motion::HoverTurn(), Milliseconds(80), Milliseconds(150)},
      {"toggle", motion::Toggle(), {}, Milliseconds(150)},
      {"drop", motion::Drop(), {}, Milliseconds(120)},
      {"panel", motion::Panel(), {}, Milliseconds(150)},
      {"sheet", motion::Sheet(), {}, Milliseconds(200)},
      {"lockIn", motion::LockIn(), {}, Milliseconds(400)},
      {"lockOut", motion::LockOut(), {}, Milliseconds(350)},
      {"fade", motion::Fade(), {}, Milliseconds(300)},
      {"status", motion::Status(), {}, Milliseconds(200)},
      {"statusAfterBar", motion::StatusAfterBar(), Milliseconds(120),
       Milliseconds(200)},
      {"barExit", motion::BarExit(), {}, Milliseconds(120)},
      {"progress", motion::Progress(), {}, Milliseconds(200)},
  };
}

}  // namespace

// Each cubic runs as long as Motion.swift's, after its delay, from 0 to 1.
TEST(MotionTest, CubicsRunAsTheSwiftsDid) {
  for (const Named& named : Table()) {
    SCOPED_TRACE(named.name);
    EXPECT_EQ(named.curve.delay(), named.delay);
    EXPECT_EQ(named.curve.duration(), named.duration);
    EXPECT_EQ(named.curve.Progress(base::TimeDelta()), 0);
    EXPECT_EQ(named.curve.Progress(named.delay), 0);
    EXPECT_EQ(named.curve.Progress(named.delay + named.duration), 1);
    EXPECT_EQ(named.curve.Progress(named.curve.total() + Milliseconds(1)), 1);
  }
}

// Halfway through, each is where SwiftUI's curve of that name is: easeOut
// (0, 0, 0.58, 1), easeIn (0.42, 0, 1, 1), easeInOut (0.42, 0, 0.58, 1),
// linear, and the lock's own (0.16, 1, 0.3, 1), each solved for its time
// apart from gfx::CubicBezier.
TEST(MotionTest, CubicsFollowSwiftUIsCurves) {
  constexpr double kEaseOutAtHalf = 0.6846431874274606;
  constexpr double kEaseInAtHalf = 0.31535681257253945;
  constexpr double kEaseInOutAtQuarter = 0.1291619310473198;
  constexpr double kLockInAtHalf = 0.9717791052635714;
  constexpr double kLockInAtTenth = 0.49439061706387855;
  constexpr double kNear = 1e-4;

  EXPECT_NEAR(motion::Hover().Progress(Milliseconds(75)), kEaseOutAtHalf,
              kNear);
  EXPECT_NEAR(motion::HoverTurn().Progress(Milliseconds(80 + 75)),
              kEaseOutAtHalf, kNear);
  EXPECT_NEAR(motion::Sheet().Progress(Milliseconds(100)), kEaseOutAtHalf,
              kNear);
  EXPECT_NEAR(motion::Fade().Progress(Milliseconds(150)), kEaseOutAtHalf,
              kNear);
  EXPECT_NEAR(motion::LockOut().Progress(Milliseconds(175)), kEaseInAtHalf,
              kNear);
  EXPECT_NEAR(motion::Status().Progress(Milliseconds(50)), kEaseInOutAtQuarter,
              kNear);
  EXPECT_NEAR(motion::StatusAfterBar().Progress(Milliseconds(120 + 50)),
              kEaseInOutAtQuarter, kNear);
  EXPECT_NEAR(motion::Status().Progress(Milliseconds(100)), 0.5, kNear);
  EXPECT_NEAR(motion::Progress().Progress(Milliseconds(50)), 0.25, kNear);
  EXPECT_NEAR(motion::LockIn().Progress(Milliseconds(200)), kLockInAtHalf,
              kNear);
  EXPECT_NEAR(motion::LockIn().Progress(Milliseconds(40)), kLockInAtTenth,
              kNear);
}

// The springs are SwiftUI's: a natural period of the response, damped by its
// fraction, from rest; a tenth of a second in and at the response, against
// the closed form worked apart, and each settled within a thousandth when it
// ends.
TEST(MotionTest, SpringsAreSwiftUIs) {
  struct Spring {
    std::string name;
    Curve curve;
    double at_tenth;
    double at_response;
    base::TimeDelta response;
    base::TimeDelta settled;
  };
  const Spring springs[] = {
      {"morph", motion::Morph(), 0.6381019501444796, 1.0049814577061145,
       Milliseconds(320), Milliseconds(447)},
      {"slide", motion::Slide(), 0.6985929520492615, 1.0003870124111407,
       Milliseconds(280), Milliseconds(374)},
      {"reveal", motion::Reveal(), 0.618941609178955, 0.986399068534425,
       Milliseconds(300), Milliseconds(441)},
  };
  for (const Spring& spring : springs) {
    SCOPED_TRACE(spring.name);
    EXPECT_EQ(spring.curve.delay(), base::TimeDelta());
    EXPECT_NEAR(spring.curve.Progress(Milliseconds(100)), spring.at_tenth,
                1e-6);
    EXPECT_NEAR(spring.curve.Progress(spring.response), spring.at_response,
                1e-6);
    EXPECT_NEAR(spring.curve.duration().InMillisecondsF(),
                spring.settled.InMillisecondsF(), 2);
    for (base::TimeDelta t = spring.curve.duration(); t < base::Seconds(2);
         t += Milliseconds(1)) {
      EXPECT_NEAR(spring.curve.Progress(t), 1, 0.001);
    }
  }
  // A morph overshoots by half a percent at most: none to speak of.
  double highest = 0;
  for (base::TimeDelta t; t < motion::Morph().total(); t += Milliseconds(1)) {
    highest = std::max(highest, motion::Morph().Progress(t));
  }
  EXPECT_LT(highest, 1.006);
}

// With less motion asked for, the drivers move nothing: a value lands at once.
TEST(MotionTest, LessMotionLandsAtOnce) {
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  EXPECT_TRUE(motion::IsReduced());
  std::vector<double> steps;
  ValueMotion value(0, base::BindLambdaForTesting(
                           [&](double step) { steps.push_back(step); }));
  value.MoveTo(1, motion::Morph(), /*compositor=*/nullptr);
  EXPECT_EQ(value.value(), 1);
  EXPECT_EQ(steps, std::vector<double>{1});
  gfx::Animation::SetPrefersReducedMotionForTesting(false);
  EXPECT_FALSE(motion::IsReduced());
}

}  // namespace pane
