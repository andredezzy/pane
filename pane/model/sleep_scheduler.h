// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_SLEEP_SCHEDULER_H_
#define PANE_MODEL_SLEEP_SCHEDULER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"

namespace base {
class Clock;
}  // namespace base

namespace pane::model {

struct AppSettings;

// What the scheduler needs from a profile: tabs whose memory it can release,
// and a cache whose disk it can release. Narrow on purpose, so the scheduler
// runs against fakes in tests.
class SleepableTabs {
 public:
  virtual ~SleepableTabs() = default;

  virtual std::vector<std::string> LoadedTabIds() const = 0;
  // Audible, DevTools open, or otherwise in use: never sleeps.
  virtual bool IsProtected(const std::string& tab_id) const = 0;
  virtual void Sleep(const std::string& tab_id) = 0;
  virtual void UnloadAll() = 0;
};

// Each call answers once, on the calling sequence; a cache that goes first
// need not.
class TrimmableCache {
 public:
  // Bytes on disk, or why they could not be read.
  using SizeCallback =
      base::OnceCallback<void(base::expected<int64_t, std::string>)>;
  // Why the trim failed, if it did.
  using TrimCallback =
      base::OnceCallback<void(base::expected<void, std::string>)>;

  virtual ~TrimmableCache() = default;

  virtual void ComputeSize(SizeCallback done) = 0;
  virtual void Trim(TrimCallback done) = 0;
};

// Puts background tabs to sleep, unloads hidden profiles once their settings
// timers elapse, and trims the cache of a profile that unloads over budget
// (CONTEXT.md: Sleep, Unload, Trim; the Electron app's sleep-scheduler.ts).
// Nothing sleeps while active, protected, or in a Keep loaded profile; a clock
// restarts when its tab or profile is activated again, so recent interaction
// is protected by construction. The app calls Tick() every kTickInterval.
class SleepScheduler {
 public:
  struct Profile {
    raw_ptr<SleepableTabs> tabs;
    base::WeakPtr<TrimmableCache> cache;
    bool keep_loaded = false;
  };

  using WarnCallback = base::RepeatingCallback<void(const std::string&)>;

  static constexpr base::TimeDelta kTickInterval = base::Minutes(1);

  // Every profile but the active one starts hidden now. `warn` hears what
  // failed; without one, the log does.
  SleepScheduler(const std::vector<std::string>& profile_ids,
                 std::optional<std::string> active_profile_id,
                 std::optional<std::string> active_tab_id,
                 const base::Clock* clock,
                 WarnCallback warn = {});
  SleepScheduler(const SleepScheduler&) = delete;
  SleepScheduler& operator=(const SleepScheduler&) = delete;
  ~SleepScheduler();

  // The selected profile or tab changed: the one left starts its clock, the
  // one shown stops it.
  void ActiveDidChange(std::optional<std::string> profile_id,
                       std::optional<std::string> tab_id);

  void Tick(const std::map<std::string, Profile>& profiles,
            const AppSettings& settings);

 private:
  void Trim(const std::string& profile_id,
            base::WeakPtr<TrimmableCache> cache,
            std::optional<int> budget_mb);
  void OnCacheSize(const std::string& profile_id,
                   base::WeakPtr<TrimmableCache> cache,
                   int64_t budget,
                   base::expected<int64_t, std::string> size);
  void OnTrimmed(const std::string& profile_id,
                 base::expected<void, std::string> result);

  // Closed tabs never become loaded again, so their stamps would pile up for
  // the life of the app without this sweep.
  void PruneTabStamps(const std::map<std::string, Profile>& profiles);

  const raw_ptr<const base::Clock> clock_;
  const WarnCallback warn_;
  std::map<std::string, base::Time> profile_hidden_since_;
  std::map<std::string, base::Time> tab_background_since_;
  // The profiles whose trim has not answered yet.
  std::set<std::string> trimming_;
  std::optional<std::string> active_profile_id_;
  std::optional<std::string> active_tab_id_;
  base::WeakPtrFactory<SleepScheduler> weak_factory_{this};
};

}  // namespace pane::model

#endif  // PANE_MODEL_SLEEP_SCHEDULER_H_
