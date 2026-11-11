// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_LOCK_TRIGGERS_H_
#define PANE_UI_APP_LOCK_TRIGGERS_H_

#include <memory>
#include <optional>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/power_monitor/power_observer.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace pane {

// When Pane locks by itself (ADR 0116, Paper 06.4; the Swift's
// LockController): when the screen locks (ui::AddScreenLockCallback), the
// computer sleeps, a lid closing it too (base::PowerMonitor's suspend), its
// displays sleep, or another user takes the session (the platform's pair,
// which nothing in Chromium observes); and after the time without use Settings
// names (Lock after). Pane is in use while it is in front and the computer has
// input, which then is Pane's (ui::CalculateIdleTime), and while a page plays
// sound or shows in picture-in-picture; behind other apps, its clock runs from
// when it left the front. No key or mouse monitor of its own (ADR 0079). Each
// asks the delegate to lock, which does nothing without a PIN or while locked;
// the Lock keeps ADR 0116's order.
class LockTriggers : public base::PowerSuspendObserver {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Lock Pane now, as Lock Pane does: nothing without a PIN, or locked.
    virtual void LockFromSystem() = 0;
    // How long without use before Pane locks: none without a PIN, while
    // locked, or with Lock after at Never.
    virtual std::optional<base::TimeDelta> LockAfter() const = 0;
    // A page plays sound or shows in picture-in-picture.
    virtual bool InUse() const = 0;
  };

  // What the platform tells Pane that nothing in Chromium hears, its pair
  // implementing it (lock_triggers_mac.mm, lock_triggers_win.cc): `lock` when
  // the displays sleep or another user takes the session, `left_front` as
  // Pane leaves the front; and whether it is in front.
  class Signals {
   public:
    static std::unique_ptr<Signals> Create(base::RepeatingClosure lock,
                                           base::RepeatingClosure left_front);

    virtual ~Signals() = default;

    virtual bool InFront() const = 0;
  };

  using SignalsFactory = base::OnceCallback<std::unique_ptr<Signals>(
      base::RepeatingClosure lock,
      base::RepeatingClosure left_front)>;
  // How long the computer has had no input.
  using IdleTime = base::RepeatingCallback<base::TimeDelta()>;

  // Its clock runs `time_scale` times faster (a scenario's PANE_TIME_SCALE).
  LockTriggers(Delegate& delegate, double time_scale);
  // With the signals and the idle time a test gives.
  LockTriggers(Delegate& delegate,
               double time_scale,
               SignalsFactory signals,
               IdleTime idle_time);
  LockTriggers(const LockTriggers&) = delete;
  LockTriggers& operator=(const LockTriggers&) = delete;
  ~LockTriggers() override;

  // The screen locked, or unlocked (ui::AddScreenLockCallback).
  void ScreenLockChanged(bool locked);

  // base::PowerSuspendObserver: the computer is about to sleep.
  void OnSuspend() override;

  // How often it looks at the time without use: 15 s, as the Swift's timer,
  // scaled.
  static constexpr base::TimeDelta kTick = base::Seconds(15);

 private:
  void Lock();
  void LeftFront();
  void Tick();
  // How long Pane has gone without use: the computer's idle time while it is
  // in front, the time since it left the front otherwise.
  base::TimeDelta Idle();

  const raw_ref<Delegate> delegate_;
  const double time_scale_;
  const IdleTime idle_time_;
  std::unique_ptr<Signals> signals_;
  // When Pane was last in front, or last could not lock.
  base::TimeTicks left_front_;
  base::RepeatingTimer timer_;
  base::CallbackListSubscription screen_lock_;
};

}  // namespace pane

#endif  // PANE_UI_APP_LOCK_TRIGGERS_H_
