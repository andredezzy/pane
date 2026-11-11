// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/lock_triggers.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/power_monitor/power_monitor.h"
#include "ui/base/idle/idle.h"

namespace pane {

namespace {

base::TimeDelta ComputerIdleTime() {
  return base::Seconds(ui::CalculateIdleTime());
}

}  // namespace

LockTriggers::LockTriggers(Delegate& delegate, double time_scale)
    : LockTriggers(delegate,
                   time_scale,
                   base::BindOnce(&Signals::Create),
                   base::BindRepeating(&ComputerIdleTime)) {}

LockTriggers::LockTriggers(Delegate& delegate,
                           double time_scale,
                           SignalsFactory signals,
                           IdleTime idle_time)
    : delegate_(delegate),
      time_scale_(time_scale),
      idle_time_(std::move(idle_time)),
      left_front_(base::TimeTicks::Now()) {
  signals_ = std::move(signals).Run(
      base::BindRepeating(&LockTriggers::Lock, base::Unretained(this)),
      base::BindRepeating(&LockTriggers::LeftFront, base::Unretained(this)));
  screen_lock_ = ui::AddScreenLockCallback(base::BindRepeating(
      &LockTriggers::ScreenLockChanged, base::Unretained(this)));
  base::PowerMonitor::GetInstance()->AddPowerSuspendObserver(this);
  timer_.Start(
      FROM_HERE, kTick / time_scale_,
      base::BindRepeating(&LockTriggers::Tick, base::Unretained(this)));
}

LockTriggers::~LockTriggers() {
  base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
}

void LockTriggers::ScreenLockChanged(bool locked) {
  if (locked) {
    Lock();
  }
}

void LockTriggers::OnSuspend() {
  Lock();
}

void LockTriggers::Lock() {
  delegate_->LockFromSystem();
}

void LockTriggers::LeftFront() {
  left_front_ = base::TimeTicks::Now();
}

void LockTriggers::Tick() {
  const std::optional<base::TimeDelta> after = delegate_->LockAfter();
  if (!after || delegate_->InUse()) {
    left_front_ = base::TimeTicks::Now();
    return;
  }
  if (Idle() * time_scale_ >= *after) {
    Lock();
  }
}

base::TimeDelta LockTriggers::Idle() {
  if (!signals_->InFront()) {
    return base::TimeTicks::Now() - left_front_;
  }
  left_front_ = base::TimeTicks::Now();
  return idle_time_.Run();
}

}  // namespace pane
