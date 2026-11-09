// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/sleep_scheduler.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/test/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "pane/model/app_settings.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's SleepSchedulerTests (the Electron app's
// sleep-scheduler.test.ts): every case, against fake tabs and caches, with a
// clock the tests move one scheduler tick at a time (the Electron tests' fake
// timers). The fake cache answers at once, where Swift's trims ran in a task
// the tests then waited for.

using Ids = std::vector<std::string>;

class FakeTabs : public SleepableTabs {
 public:
  explicit FakeTabs(Ids tab_ids = {"tab-1"}) : loaded(std::move(tab_ids)) {}

  Ids LoadedTabIds() const override { return loaded_override.value_or(loaded); }

  bool IsProtected(const std::string& tab_id) const override {
    return is_protected.Run(tab_id);
  }

  void Sleep(const std::string& tab_id) override {
    slept.push_back(tab_id);
    std::erase(loaded, tab_id);
  }

  void UnloadAll() override {
    ++unloads;
    loaded.clear();
    loaded_override.reset();
  }

  Ids loaded;
  // Replaces the loaded tabs the scheduler sees (the Electron tests'
  // mockReturnValue).
  std::optional<Ids> loaded_override;
  base::RepeatingCallback<bool(const std::string&)> is_protected =
      base::BindRepeating([](const std::string&) { return false; });
  Ids slept;
  int unloads = 0;
};

class FakeCache : public TrimmableCache {
 public:
  enum class Trims { kSucceed, kNeverFinish, kFail };

  explicit FakeCache(int size_mb = 0) : size_mb_(size_mb) {}

  void ComputeSize(SizeCallback done) override {
    ++size_reads;
    std::move(done).Run(int64_t{size_mb_} * 1024 * 1024);
  }

  void Trim(TrimCallback done) override {
    ++trims;
    switch (trim_behavior) {
      case Trims::kSucceed:
        std::move(done).Run(base::ok());
        return;
      case Trims::kNeverFinish:
        unanswered_ = std::move(done);
        return;
      case Trims::kFail:
        std::move(done).Run(base::unexpected("the disk is busy"));
        return;
    }
  }

  base::WeakPtr<FakeCache> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  Trims trim_behavior = Trims::kSucceed;
  int size_reads = 0;
  int trims = 0;

 private:
  const int size_mb_;
  TrimCallback unanswered_;
  base::WeakPtrFactory<FakeCache> weak_factory_{this};
};

// A profile's fakes, the harness's own where none is given, and whether it is
// Keep loaded.
struct Fakes {
  raw_ptr<FakeTabs> tabs;
  raw_ptr<FakeCache> cache;
  bool keep_loaded = false;
};

// A scheduler with its fake clock and settings, ticking every minute as the
// app's timer does.
class Harness {
 public:
  Harness() {
    clock_.SetNow(base::Time::FromSecondsSinceUnixEpoch(1'800'000'000));
    settings.tab_sleep_after_minutes = 15;
    settings.profile_unload_after_minutes = 30;
    settings.cache_budget_mb = 300;
  }

  void Add(const std::string& id, Fakes fakes = {}) {
    if (!fakes.tabs) {
      fakes.tabs = own_tabs_.emplace_back(std::make_unique<FakeTabs>()).get();
    }
    if (!fakes.cache) {
      fakes.cache =
          own_caches_.emplace_back(std::make_unique<FakeCache>()).get();
    }
    profiles_[id] = {.tabs = fakes.tabs.get(),
                     .cache = fakes.cache->GetWeakPtr(),
                     .keep_loaded = fakes.keep_loaded};
  }

  void Start(std::optional<std::string> active_profile = std::nullopt,
             std::optional<std::string> active_tab = std::nullopt) {
    Ids profile_ids;
    for (const auto& [id, profile] : profiles_) {
      profile_ids.push_back(id);
    }
    scheduler_ = std::make_unique<SleepScheduler>(
        profile_ids, std::move(active_profile), std::move(active_tab), &clock_,
        base::BindLambdaForTesting([this](const std::string& warning) {
          warnings.push_back(warning);
        }));
  }

  void Activate(std::optional<std::string> tab,
                std::optional<std::string> profile) {
    scheduler_->ActiveDidChange(std::move(profile), std::move(tab));
  }

  void Advance(int minutes) {
    for (int minute = 0; minute < minutes; ++minute) {
      clock_.Advance(base::Minutes(1));
      scheduler_->Tick(profiles_, settings);
    }
  }

  AppSettings settings;
  Ids warnings;

 private:
  base::SimpleTestClock clock_;
  std::vector<std::unique_ptr<FakeTabs>> own_tabs_;
  std::vector<std::unique_ptr<FakeCache>> own_caches_;
  std::map<std::string, SleepScheduler::Profile> profiles_;
  std::unique_ptr<SleepScheduler> scheduler_;
};

// SleepScheduler · profile unload.

// Unloads a hidden profile once the timer elapses.
TEST(ProfileUnloadTest, UnloadsHidden) {
  // Tab sleep stays off, so it cannot drain the profile first: this isolates
  // the profile clock.
  Harness harness;
  harness.settings.tab_sleep_after_minutes = std::nullopt;
  FakeTabs tabs;
  harness.Add("hidden", {.tabs = &tabs});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 1);
}

// Never unloads the active profile.
TEST(ProfileUnloadTest, NeverActive) {
  Harness harness;
  FakeTabs tabs;
  harness.Add("active", {.tabs = &tabs});
  harness.Start("active", "tab-1");

  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 0);
}

// Skips Keep loaded profiles.
TEST(ProfileUnloadTest, SkipsKeepLoaded) {
  Harness harness;
  FakeTabs tabs;
  harness.Add("kept", {.tabs = &tabs, .keep_loaded = true});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 0);
  EXPECT_TRUE(tabs.slept.empty());
}

// Skips profiles holding a protected tab.
TEST(ProfileUnloadTest, SkipsProtected) {
  Harness harness;
  FakeTabs tabs;
  tabs.is_protected =
      base::BindRepeating([](const std::string&) { return true; });
  harness.Add("hidden", {.tabs = &tabs});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 0);
}

// Skips already-unloaded profiles.
TEST(ProfileUnloadTest, SkipsUnloaded) {
  Harness harness;
  FakeTabs tabs(Ids{});
  harness.Add("hidden", {.tabs = &tabs});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 0);
}

// Does nothing when the timer setting is off.
TEST(ProfileUnloadTest, OffSetting) {
  Harness harness;
  harness.settings.profile_unload_after_minutes = std::nullopt;
  FakeTabs tabs;
  harness.Add("hidden", {.tabs = &tabs});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 0);
}

// Restarts the hidden clock when a profile is re-activated.
TEST(ProfileUnloadTest, RestartsHiddenClock) {
  // Tab sleep stays off, so it cannot drain the profile first: this isolates
  // the profile clock.
  Harness harness;
  harness.settings.tab_sleep_after_minutes = std::nullopt;
  FakeTabs tabs;
  harness.Add("profile", {.tabs = &tabs});
  harness.Start();

  // 20 minutes hidden, then a visit resets the clock; 20 more minutes after
  // switching away must not reach the 30-minute deadline.
  harness.Advance(20);
  harness.Activate("tab-1", "profile");
  harness.Activate(std::nullopt, std::nullopt);
  harness.Advance(20);

  EXPECT_EQ(tabs.unloads, 0);

  harness.Advance(11);

  EXPECT_EQ(tabs.unloads, 1);
}

// SleepScheduler · tab sleep.

// Sleeps a background tab once the timer elapses.
TEST(TabSleepTest, SleepsBackground) {
  Harness harness;
  FakeTabs tabs({"tab-1", "tab-2"});
  harness.Add("active", {.tabs = &tabs});
  harness.Start();

  // tab-1 goes to the background when tab-2 takes over.
  harness.Activate("tab-1", "active");
  harness.Activate("tab-2", "active");
  harness.Advance(16);

  EXPECT_EQ(tabs.slept, Ids{"tab-1"});
}

// Sleeps a tab loaded in the background that was never active.
TEST(TabSleepTest, SleepsNeverActive) {
  Harness harness;
  FakeTabs tabs({"tab-1"});
  harness.Add("active", {.tabs = &tabs});
  harness.Start();

  harness.Activate("tab-1", "active");
  // A link opened in a new tab: loaded, never selected.
  tabs.loaded_override = Ids{"tab-1", "tab-2"};
  harness.Advance(15);

  EXPECT_TRUE(tabs.slept.empty());

  harness.Advance(1);

  EXPECT_EQ(tabs.slept, Ids{"tab-2"});
}

// Never sleeps the active tab.
TEST(TabSleepTest, NeverActive) {
  Harness harness;
  FakeTabs tabs({"tab-1"});
  harness.Add("active", {.tabs = &tabs});
  harness.Start();

  harness.Activate("tab-1", "active");
  harness.Advance(16);

  EXPECT_TRUE(tabs.slept.empty());
}

// Skips protected background tabs.
TEST(TabSleepTest, SkipsProtected) {
  Harness harness;
  FakeTabs tabs({"tab-1", "tab-2"});
  tabs.is_protected = base::BindRepeating(
      [](const std::string& tab_id) { return tab_id == "tab-1"; });
  harness.Add("active", {.tabs = &tabs});
  harness.Start();

  harness.Activate("tab-1", "active");
  harness.Activate("tab-2", "active");
  harness.Advance(16);

  EXPECT_TRUE(tabs.slept.empty());
}

// Does nothing when the tab timer setting is off.
TEST(TabSleepTest, OffSetting) {
  Harness harness;
  harness.settings.tab_sleep_after_minutes = std::nullopt;
  FakeTabs tabs({"tab-1", "tab-2"});
  harness.Add("active", {.tabs = &tabs});
  harness.Start();

  harness.Activate("tab-1", "active");
  harness.Activate("tab-2", "active");
  harness.Advance(31);

  EXPECT_TRUE(tabs.slept.empty());
}

// Restarts the background clock when a tab is re-activated.
TEST(TabSleepTest, RestartsBackgroundClock) {
  Harness harness;
  FakeTabs tabs({"tab-1", "tab-2"});
  harness.Add("active", {.tabs = &tabs});
  harness.Start();

  harness.Activate("tab-1", "active");
  harness.Activate("tab-2", "active");
  harness.Advance(10);
  harness.Activate("tab-1", "active");
  harness.Activate("tab-2", "active");
  harness.Advance(10);

  EXPECT_TRUE(tabs.slept.empty());

  harness.Advance(6);

  EXPECT_EQ(tabs.slept, Ids{"tab-1"});
}

// SleepScheduler · cache trim. The trim hangs off the unload, so every case
// advances past the 30-minute unload deadline.

// Trims a profile that unloads over budget.
TEST(CacheTrimTest, OverBudget) {
  Harness harness;
  FakeCache cache(400);
  harness.Add("hidden", {.cache = &cache});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(cache.trims, 1);
}

// Leaves a profile that unloads under budget.
TEST(CacheTrimTest, UnderBudget) {
  Harness harness;
  FakeCache cache(50);
  harness.Add("hidden", {.cache = &cache});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(cache.size_reads, 1);
  EXPECT_EQ(cache.trims, 0);
}

// Never measures the cache when the budget setting is off.
TEST(CacheTrimTest, BudgetOff) {
  Harness harness;
  harness.settings.cache_budget_mb = std::nullopt;
  FakeCache cache(4000);
  harness.Add("hidden", {.cache = &cache});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(cache.size_reads, 0);
  EXPECT_EQ(cache.trims, 0);
}

// Never trims a Keep loaded profile, however large.
TEST(CacheTrimTest, KeepLoaded) {
  Harness harness;
  FakeCache cache(4000);
  harness.Add("kept", {.cache = &cache, .keep_loaded = true});
  harness.Start();

  harness.Advance(31);

  EXPECT_EQ(cache.trims, 0);
}

// Never trims on the tab-sleep path.
TEST(CacheTrimTest, TabSleepPath) {
  Harness harness;
  harness.settings.profile_unload_after_minutes = std::nullopt;
  FakeTabs tabs({"tab-1", "tab-2"});
  FakeCache cache(4000);
  harness.Add("active", {.tabs = &tabs, .cache = &cache});
  harness.Start();

  harness.Activate("tab-1", "active");
  harness.Activate("tab-2", "active");
  harness.Advance(31);

  EXPECT_FALSE(tabs.slept.empty());
  EXPECT_EQ(cache.trims, 0);
}

// Trims a hidden profile that tab sleep drained. Not in the Electron tests:
// its scheduler never trimmed this profile.
TEST(CacheTrimTest, DrainedByTabSleep) {
  Harness harness;
  FakeTabs tabs({"tab-1", "tab-2"});
  FakeCache cache(400);
  harness.Add("hidden", {.tabs = &tabs, .cache = &cache});
  harness.Start();

  // Shown, then hidden: both its tabs are in the background.
  harness.Activate("tab-1", "hidden");
  harness.Activate("tab-2", "hidden");
  harness.Activate(std::nullopt, std::nullopt);
  harness.Advance(16);

  Ids slept = tabs.slept;
  std::ranges::sort(slept);
  EXPECT_EQ(slept, (Ids{"tab-1", "tab-2"}));
  EXPECT_EQ(tabs.unloads, 0);
  EXPECT_EQ(cache.trims, 1);
}

// Leaves a drained profile's cache when unloading is off.
TEST(CacheTrimTest, DrainedWithUnloadOff) {
  Harness harness;
  harness.settings.profile_unload_after_minutes = std::nullopt;
  FakeTabs tabs({"tab-1"});
  FakeCache cache(400);
  harness.Add("hidden", {.tabs = &tabs, .cache = &cache});
  harness.Start();

  harness.Activate("tab-1", "hidden");
  harness.Activate(std::nullopt, std::nullopt);
  harness.Advance(16);

  EXPECT_EQ(tabs.slept, Ids{"tab-1"});
  EXPECT_EQ(cache.size_reads, 0);
}

// Does not stack a second trim while one is in flight.
TEST(CacheTrimTest, NoStacking) {
  // Tab sleep stays off, so the trims come from unloads alone.
  Harness harness;
  harness.settings.tab_sleep_after_minutes = std::nullopt;
  FakeTabs tabs;
  FakeCache cache(400);
  // The first trim stays in flight across later ticks.
  cache.trim_behavior = FakeCache::Trims::kNeverFinish;
  harness.Add("hidden", {.tabs = &tabs, .cache = &cache});
  harness.Start();

  harness.Advance(31);

  // Hidden again, so the unload is reachable on a later tick.
  harness.Activate("tab-1", "hidden");
  harness.Activate(std::nullopt, std::nullopt);
  tabs.loaded_override = Ids{"tab-1"};
  harness.Advance(31);

  EXPECT_EQ(tabs.unloads, 2);
  EXPECT_EQ(cache.trims, 1);
}

// Keeps ticking after a trim fails.
TEST(CacheTrimTest, AfterFailure) {
  Harness harness;
  FakeTabs tabs;
  FakeCache cache(400);
  cache.trim_behavior = FakeCache::Trims::kFail;
  harness.Add("hidden", {.tabs = &tabs, .cache = &cache});
  harness.Start();

  harness.Advance(31);

  EXPECT_FALSE(harness.warnings.empty());

  // The in-flight guard let go, so a later unload trims again.
  harness.Activate("tab-1", "hidden");
  harness.Activate(std::nullopt, std::nullopt);
  tabs.loaded_override = Ids{"tab-1"};
  harness.Advance(31);

  EXPECT_EQ(cache.trims, 2);
}

}  // namespace
}  // namespace pane::model
