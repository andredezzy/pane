// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_SLEEP_CONTROLLER_H_
#define PANE_UI_APP_SLEEP_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ref.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "pane/model/sleep_scheduler.h"

class Profile;

namespace content {
class WebContents;
}

namespace pane {

namespace model {
class BrowserModel;
}

// Runs the sleep scheduler over the open profiles (CONTEXT.md: Sleep,
// Unload, Trim, Keep loaded; ADR 0041), as the Swift's SleepController did: a
// timer ticks it every minute, PANE_TIME_SCALE times faster in a scenario's
// run, and the selection moves its clocks. A tab sleeps as Unload releases
// it: its page goes and its tab stays, dimmed, until selecting it loads its
// address again. Playing sound, inspected in DevTools, or in picture in
// picture, it is in use and never sleeps.
class SleepController {
 public:
  // What it sleeps and unloads: the app's.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The profile in the engine, while it is open.
    virtual Profile* ProfileIfOpen(const std::string& profile_id) const = 0;
    // The tab's page, when it has one.
    virtual content::WebContents* PageOf(const std::string& tab_id) const = 0;
    // The tab's page goes, its tab stays.
    virtual void SleepTab(const std::string& tab_id) = 0;
    // Every page of the profile goes, its tabs stay.
    virtual void UnloadProfile(const std::string& profile_id) = 0;
  };

  // `model` and `delegate` outlive it.
  SleepController(const model::BrowserModel& model, Delegate& delegate);
  SleepController(const SleepController&) = delete;
  SleepController& operator=(const SleepController&) = delete;
  ~SleepController();

  // The selected tab or profile changed: the one left starts its clock, the
  // one shown stops it.
  void SelectionChanged();

 private:
  // The wall clock, `scale` times faster from when it was made.
  class ScaledClock : public base::Clock {
   public:
    explicit ScaledClock(double scale);
    base::Time Now() const override;

   private:
    const base::Time start_;
    const double scale_;
  };

  class ProfileTabs;
  class ProfileCache;

  void Tick();

  const raw_ref<const model::BrowserModel> model_;
  const raw_ref<Delegate> delegate_;
  ScaledClock clock_;
  model::SleepScheduler scheduler_;
  // Each open profile's cache, kept across ticks, since a trim answers after
  // the tick that started it.
  std::map<std::string, std::unique_ptr<ProfileCache>> caches_;
  base::RepeatingTimer timer_;
};

}  // namespace pane

#endif  // PANE_UI_APP_SLEEP_CONTROLLER_H_
