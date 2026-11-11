// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/lock_triggers.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/test/power_monitor_test.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {

namespace {

// Whether Pane is in front, as the test has it.
class FakeSignals : public LockTriggers::Signals {
 public:
  explicit FakeSignals(const bool* in_front) : in_front_(in_front) {}

  bool InFront() const override { return *in_front_; }

 private:
  const raw_ptr<const bool> in_front_;
};

class FakeDelegate : public LockTriggers::Delegate {
 public:
  void LockFromSystem() override { ++locks; }
  std::optional<base::TimeDelta> LockAfter() const override {
    return lock_after;
  }
  bool InUse() const override { return in_use; }

  int locks = 0;
  std::optional<base::TimeDelta> lock_after;
  bool in_use = false;
};

class LockTriggersTest : public testing::Test {
 protected:
  std::unique_ptr<LockTriggers> Make(double time_scale = 1) {
    return std::make_unique<LockTriggers>(
        delegate_, time_scale,
        base::BindLambdaForTesting(
            [this](base::RepeatingClosure lock,
                   base::RepeatingClosure left_front)
                -> std::unique_ptr<LockTriggers::Signals> {
              lock_ = std::move(lock);
              left_front_ = std::move(left_front);
              return std::make_unique<FakeSignals>(&in_front_);
            }),
        base::BindLambdaForTesting([this] { return idle_; }));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::test::ScopedPowerMonitorTestSource power_;
  FakeDelegate delegate_;
  bool in_front_ = false;
  base::TimeDelta idle_;
  base::RepeatingClosure lock_;
  base::RepeatingClosure left_front_;
};

}  // namespace

// The computer about to sleep (a lid closing too), the screen locking, the
// displays sleeping, and another user taking the session each lock Pane; the
// screen unlocking does nothing.
TEST_F(LockTriggersTest, SystemEventsLock) {
  const std::unique_ptr<LockTriggers> triggers = Make();
  power_.GenerateSuspendEvent();
  EXPECT_EQ(delegate_.locks, 1);
  power_.GenerateResumeEvent();
  triggers->ScreenLockChanged(true);
  EXPECT_EQ(delegate_.locks, 2);
  triggers->ScreenLockChanged(false);
  EXPECT_EQ(delegate_.locks, 2);
  lock_.Run();
  EXPECT_EQ(delegate_.locks, 3);
}

// Behind other apps, the clock runs from when Pane left the front: Lock
// after's time later, it locks, not before.
TEST_F(LockTriggersTest, BehindLocksAfterTheTimeSinceItLeftTheFront) {
  delegate_.lock_after = base::Minutes(1);
  const std::unique_ptr<LockTriggers> triggers = Make();
  left_front_.Run();
  task_environment_.FastForwardBy(base::Seconds(45));
  EXPECT_EQ(delegate_.locks, 0);
  task_environment_.FastForwardBy(base::Seconds(15));
  EXPECT_EQ(delegate_.locks, 1);
}

// In front, the computer's own idle time counts: input keeps it open.
TEST_F(LockTriggersTest, InFrontCountsTheComputersIdleTime) {
  delegate_.lock_after = base::Minutes(1);
  in_front_ = true;
  const std::unique_ptr<LockTriggers> triggers = Make();
  idle_ = base::Seconds(10);
  task_environment_.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(delegate_.locks, 0);
  idle_ = base::Seconds(60);
  task_environment_.FastForwardBy(LockTriggers::kTick);
  EXPECT_EQ(delegate_.locks, 1);
}

// A page playing sound or in picture-in-picture is use: the clock starts
// over, and runs again once it stops.
TEST_F(LockTriggersTest, AudibleOrPictureInPictureIsUse) {
  delegate_.lock_after = base::Minutes(1);
  delegate_.in_use = true;
  const std::unique_ptr<LockTriggers> triggers = Make();
  task_environment_.FastForwardBy(base::Minutes(10));
  EXPECT_EQ(delegate_.locks, 0);
  delegate_.in_use = false;
  task_environment_.FastForwardBy(base::Seconds(45));
  EXPECT_EQ(delegate_.locks, 0);
  task_environment_.FastForwardBy(base::Seconds(15));
  EXPECT_EQ(delegate_.locks, 1);
}

// With no time to lock after (Never, no PIN, or locked), the clock waits,
// and starts from then once there is one.
TEST_F(LockTriggersTest, NoTimeWaits) {
  const std::unique_ptr<LockTriggers> triggers = Make();
  task_environment_.FastForwardBy(base::Hours(2));
  EXPECT_EQ(delegate_.locks, 0);
  delegate_.lock_after = base::Minutes(5);
  task_environment_.FastForwardBy(base::Minutes(4));
  EXPECT_EQ(delegate_.locks, 0);
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_EQ(delegate_.locks, 1);
}

// A scenario's clock runs faster: a minute a second at 60 times.
TEST_F(LockTriggersTest, TimeScaleRunsTheClockFaster) {
  delegate_.lock_after = base::Minutes(1);
  const std::unique_ptr<LockTriggers> triggers = Make(/*time_scale=*/60);
  task_environment_.FastForwardBy(base::Milliseconds(750));
  EXPECT_EQ(delegate_.locks, 0);
  task_environment_.FastForwardBy(base::Milliseconds(500));
  EXPECT_GE(delegate_.locks, 1);
}

}  // namespace pane
