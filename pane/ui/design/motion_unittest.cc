// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/motion.h"

#include <algorithm>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/transform.h"

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

// From rest, a change goes where its progress says, a spring included.
TEST(MotionTest, AChangeFromRestFollowsItsProgress) {
  for (const Curve& curve :
       {motion::Slide(), motion::Morph(), motion::Reveal(), motion::Hover()}) {
    for (int ms = 0; ms <= 500; ms += 7) {
      const base::TimeDelta t = Milliseconds(ms);
      EXPECT_NEAR(curve.At(40, 120, 0, t).value, 40 + 80 * curve.Progress(t),
                  1e-9);
    }
  }
}

// A spring that takes over a change under way starts where it stood and as
// fast as it moved, as SwiftUI merges one into another: the dragged row
// heading for one place, then for the next, never stops dead between them.
TEST(MotionTest, ASpringKeepsTheSpeedOfTheChangeItTakesOver) {
  const Curve slide = motion::Slide();
  // 60 ms into a slide 40 points down, the target moves 40 more.
  const Curve::Point midway = slide.At(0, 40, 0, Milliseconds(60));
  ASSERT_GT(midway.velocity, 100);
  const double from = midway.value;
  const double speed = midway.velocity;
  const Curve::Point start = slide.At(from, 80, speed, base::TimeDelta());
  EXPECT_DOUBLE_EQ(start.value, from);
  EXPECT_NEAR(start.velocity, speed, 1e-9);
  // Its speed is its value's change, a millisecond on.
  const Curve::Point later = slide.At(from, 80, speed, Milliseconds(1));
  EXPECT_NEAR((later.value - from) / 0.001, speed, speed * 0.05);
  // Each point's speed, against the change in its value around it.
  for (int ms = 1; ms < 400; ms += 13) {
    const double before =
        slide.At(from, 80, speed, Milliseconds(ms) - base::Microseconds(50))
            .value;
    const double after =
        slide.At(from, 80, speed, Milliseconds(ms) + base::Microseconds(50))
            .value;
    EXPECT_NEAR(slide.At(from, 80, speed, Milliseconds(ms)).velocity,
                (after - before) / 0.0001, 0.05);
  }
  // It settles where it's going, within a thousandth, and stays.
  base::TimeDelta settled;
  while (!slide.Settled(from, 80, speed, settled)) {
    settled += Milliseconds(1);
    ASSERT_LT(settled, base::Seconds(2));
  }
  EXPECT_GT(settled, Milliseconds(200));
  EXPECT_LT(settled, Milliseconds(600));
  const double near =
      0.001 * std::max(80 - from, speed / (2 * std::numbers::pi / 0.28));
  for (base::TimeDelta t = settled; t < base::Seconds(2);
       t += Milliseconds(1)) {
    EXPECT_NEAR(slide.At(from, 80, speed, t).value, 80, near);
  }
  // A cubic takes over at rest.
  EXPECT_DOUBLE_EQ(motion::Hover().At(10, 20, 500, base::TimeDelta()).velocity,
                   0);
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

// With nothing drawing the layer, a compositor motion lands at once and runs
// `done`, taking over what moved the property it sets and nothing else.
TEST(MotionTest, ACompositorMotionWithNothingDrawingLandsAtOnce) {
  std::unique_ptr<::ui::Layer> layer =
      ::ui::Layer::Create(::ui::LAYER_NOT_DRAWN);
  ::ui::LayerAnimator* animator = layer->GetAnimator();
  // Something moving the transform for a second.
  animator->StartAnimation(new ::ui::LayerAnimationSequence(
      ::ui::LayerAnimationElement::CreatePauseElement(
          ::ui::LayerAnimationElement::TRANSFORM, base::Seconds(1))));
  int done = 0;

  CompositorMotion::Start(layer.get(), motion::Sheet(), {.opacity = 0.5f},
                          base::BindLambdaForTesting([&] { ++done; }));
  EXPECT_EQ(layer->opacity(), 0.5f);
  EXPECT_EQ(done, 1);
  EXPECT_TRUE(
      animator->IsAnimatingProperty(::ui::LayerAnimationElement::TRANSFORM));

  gfx::Transform grown;
  grown.Scale(2, 2);
  CompositorMotion::Start(layer.get(), motion::Sheet(), {.transform = grown},
                          base::BindLambdaForTesting([&] { ++done; }));
  EXPECT_EQ(layer->transform(), grown);
  EXPECT_EQ(done, 2);
  EXPECT_FALSE(animator->is_animating());
}

}  // namespace pane
