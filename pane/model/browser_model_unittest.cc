// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/browser_model.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "pane/model/app_settings.h"
#include "pane/model/profile.h"
#include "pane/model/profile_form.h"
#include "pane/model/tab_history.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's BrowserModelTests. The Swift tests' ManualClock is
// split in two: the stamps' clock, set by hand, and a task environment whose
// time the tests move, where a waiting save is the main thread's pending
// task. Swift's selection observers are the model's observers here.
//
// "an unloaded tab keeps nothing of its page" is not ported, nor what
// "removing a tab…" checked of the tab's page: what a live page is lives in
// its WebContents, which an unload or a close destroys, and the Swift model's
// mirrors of it are not ported (docs/plans/views-migration.md, What never gets
// ported).

using Ids = std::vector<std::string>;

BrowserProfile MakeProfile(const std::string& id, const Ids& tab_ids = {}) {
  BrowserProfile profile;
  profile.id = id;
  profile.name = id;
  for (const std::string& tab_id : tab_ids) {
    profile.tabs.push_back({.id = tab_id,
                            .url = "https://" + tab_id + ".example/",
                            .title = base::ToUpperASCII(tab_id)});
  }
  return profile;
}

Ids TabIds(const BrowserModel& model, const std::string& profile_id) {
  Ids ids;
  if (const BrowserProfile* profile = model.FindProfile(profile_id)) {
    for (const Tab& tab : profile->tabs) {
      ids.push_back(tab.id);
    }
  }
  return ids;
}

Ids ProfileIds(const std::vector<BrowserProfile>& profiles) {
  Ids ids;
  for (const BrowserProfile& profile : profiles) {
    ids.push_back(profile.id);
  }
  return ids;
}

// Counts the changes of the selection it hears.
class SelectionCount : public BrowserModel::Observer {
 public:
  explicit SelectionCount(BrowserModel& model) { observation_.Observe(&model); }

  void OnActiveTabChanged() override { ++count; }
  void OnActiveProfileChanged() override { ++count; }

  int count = 0;

 private:
  base::ScopedObservation<BrowserModel, BrowserModel::Observer> observation_{
      this};
};

// The events it hears, in order, a selection's with the selection it finds:
// "tab, b in q".
class Events : public BrowserModel::Observer {
 public:
  explicit Events(BrowserModel& model) : model_(model) {
    observation_.Observe(&model);
  }

  // What it heard since the last call.
  Ids Take() { return std::exchange(heard_, {}); }

  void OnProfilesChanged() override { heard_.push_back("profiles"); }
  void OnSettingsChanged() override { heard_.push_back("settings"); }
  void OnExpandedProfilesChanged() override { heard_.push_back("expanded"); }
  void OnProxyStatusChanged() override { heard_.push_back("proxy status"); }
  void OnPageChanged() override { heard_.push_back("page"); }
  void OnActiveTabChanged() override { heard_.push_back("tab, " + Selected()); }
  void OnActiveProfileChanged() override {
    heard_.push_back("profile, " + Selected());
  }

 private:
  std::string Selected() const {
    return model_->active_tab_id().value_or("none") + " in " +
           model_->active_profile_id().value_or("none");
  }

  const raw_ref<const BrowserModel> model_;
  Ids heard_;
  base::ScopedObservation<BrowserModel, BrowserModel::Observer> observation_{
      this};
};

class BrowserModelTest : public testing::Test {
 protected:
  BrowserModelTest() {
    clock_.SetNow(base::Time::FromSecondsSinceUnixEpoch(1'800'000'000));
  }

  // The model, over `profiles` and the default settings, its saves kept.
  BrowserModel& Start(std::vector<BrowserProfile> profiles = {}) {
    model_ = std::make_unique<BrowserModel>(
        std::move(profiles), AppSettings(), &clock_,
        base::BindLambdaForTesting([this](BrowserModel::Save save) {
          saves_.push_back(std::move(save));
        }));
    return *model_;
  }

  // The saves waiting (the Swift tests' clock.waiting).
  size_t Waiting() const {
    return task_environment_.GetPendingMainThreadTaskCount();
  }

  // Runs what waits, as its delay passing does.
  void Fire() { task_environment_.FastForwardBy(BrowserModel::kSaveDelay); }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::SimpleTestClock clock_;
  std::vector<BrowserModel::Save> saves_;
  std::unique_ptr<BrowserModel> model_;
};

// Tabs.

// inserts a tab after its anchor, else at the end
TEST_F(BrowserModelTest, InsertsAfterAnchor) {
  BrowserModel& model = Start({MakeProfile("p", {"a", "b", "c"})});

  model.InsertTab({.id = "x"}, "p", "a");
  EXPECT_EQ(TabIds(model, "p"), (Ids{"a", "x", "b", "c"}));

  model.InsertTab({.id = "y"}, "p");
  EXPECT_EQ(TabIds(model, "p"), (Ids{"a", "x", "b", "c", "y"}));

  // An anchor that is not the profile's puts it last.
  model.InsertTab({.id = "z"}, "p", "elsewhere");
  EXPECT_EQ(TabIds(model, "p").back(), "z");

  // No such profile: nothing.
  model.InsertTab({.id = "w"}, "none");
  EXPECT_EQ(model.FindTab("w"), nullptr);
}

// removing a tab remembers it, forgets its use, and clears its selection
TEST_F(BrowserModelTest, RemovesToHistory) {
  BrowserModel& model = Start({MakeProfile("p", {"a", "b"})});
  model.UpdateTab("a",
                  [](Tab& tab) { tab.favicon = "data:image/png;base64,AA"; });
  model.SelectTab("b");
  model.SelectTab("a");

  const std::optional<Tab> removed = model.RemoveTab("a");

  ASSERT_TRUE(removed);
  EXPECT_EQ(removed->id, "a");
  EXPECT_EQ(TabIds(model, "p"), Ids{"b"});
  EXPECT_EQ(model.active_tab_id(), std::nullopt);
  // The profile stays active: its next tab takes the place.
  EXPECT_EQ(model.active_profile_id(), "p");
  EXPECT_EQ(model.history().mru(), Ids{"b"});
  ASSERT_FALSE(model.history().closed().empty());
  EXPECT_EQ(model.history().closed().front(),
            (ClosedTab{.url = "https://a.example/", .profile_id = "p"}));

  const std::optional<ClosedTab> reopened = model.PopClosedTab();
  ASSERT_TRUE(reopened);
  EXPECT_EQ(reopened->url, "https://a.example/");
  EXPECT_EQ(model.PopClosedTab(), std::nullopt);
  EXPECT_EQ(model.RemoveTab("none"), std::nullopt);
}

// removing a profile forgets its tabs, collapses it, and clears its selection
TEST_F(BrowserModelTest, RemovesProfile) {
  BrowserModel& model =
      Start({MakeProfile("p", {"a"}), MakeProfile("q", {"b"})});
  model.SetExpanded("p", true);
  model.SetProxyStatus("p", BrowserModel::ProxyStatus::kConnected);
  model.SelectTab("b");
  model.SelectTab("a");

  model.RemoveProfile("p");

  EXPECT_EQ(ProfileIds(model.profiles()), Ids{"q"});
  EXPECT_EQ(model.active_tab_id(), std::nullopt);
  EXPECT_EQ(model.active_profile_id(), std::nullopt);
  EXPECT_TRUE(model.expanded_profile_ids().empty());
  EXPECT_FALSE(model.proxy_status().contains("p"));
  EXPECT_EQ(model.history().mru(), Ids{"b"});
  // Deleting a profile is no closed tab.
  EXPECT_TRUE(model.history().closed().empty());
}

// selecting a tab makes its profile active and it the most recent
TEST_F(BrowserModelTest, Selects) {
  BrowserModel& model =
      Start({MakeProfile("p", {"a"}), MakeProfile("q", {"b"})});

  model.SelectTab("a");
  model.SelectTab("b");

  EXPECT_EQ(model.active_tab_id(), "b");
  EXPECT_EQ(model.active_profile_id(), "q");
  ASSERT_TRUE(model.ActiveTab());
  EXPECT_EQ(model.ActiveTab()->id, "b");
  EXPECT_EQ(model.history().mru(), (Ids{"b", "a"}));

  // No tab keeps the profile.
  model.SelectTab(std::nullopt);
  EXPECT_EQ(model.active_tab_id(), std::nullopt);
  EXPECT_EQ(model.active_profile_id(), "q");
}

// every change of the selection reaches each observer
TEST_F(BrowserModelTest, SelectionObservers) {
  BrowserModel& model = Start({MakeProfile("p", {"a", "b"})});
  SelectionCount first(model);
  SelectionCount second(model);

  // The tab, then its profile.
  model.SelectTab("a");
  EXPECT_EQ(first.count, 2);

  model.SetActiveProfile(std::nullopt);
  model.RemoveTab("b");
  EXPECT_EQ(first.count, 3);

  // The selected tab going clears it.
  model.SelectTab("a");
  const int before = first.count;
  model.RemoveTab("a");
  EXPECT_EQ(first.count, before + 1);
  EXPECT_EQ(second.count, first.count);
}

// Not in PaneKit, whose ids were values: an id the model holds itself (a
// tab's, a profile's) names what goes, though the removal moves or destroys
// it.
TEST_F(BrowserModelTest, RemovesByItsOwnId) {
  BrowserModel& model =
      Start({MakeProfile("p", {"a", "b"}), MakeProfile("q", {"c"})});
  model.SelectTab("a");

  model.RemoveTab(model.ActiveTab()->id);

  EXPECT_EQ(TabIds(model, "p"), Ids{"b"});
  EXPECT_EQ(model.active_tab_id(), std::nullopt);
  EXPECT_EQ(model.history().mru(), Ids{});

  model.SetExpanded("q", true);
  model.SetProxyStatus("q", BrowserModel::ProxyStatus::kConnected);

  model.RemoveProfile(model.profiles().front().id);

  EXPECT_EQ(ProfileIds(model.profiles()), Ids{"q"});
  EXPECT_EQ(model.active_profile_id(), std::nullopt);
  EXPECT_EQ(model.expanded_profile_ids(), std::set<std::string>{"q"});
  EXPECT_TRUE(model.proxy_status().contains("q"));
}

// moves clamp past either end, and ignore a source that is not there
TEST_F(BrowserModelTest, MovesClamp) {
  BrowserModel& model = Start(
      {MakeProfile("p", {"a", "b", "c"}), MakeProfile("q"), MakeProfile("r")});

  model.MoveTab("p", 0, 99);
  EXPECT_EQ(TabIds(model, "p"), (Ids{"b", "c", "a"}));

  model.MoveTab("p", 2, -5);
  EXPECT_EQ(TabIds(model, "p"), (Ids{"a", "b", "c"}));

  model.MoveTab("p", 7, 0);
  model.MoveTab("none", 0, 1);
  EXPECT_EQ(TabIds(model, "p"), (Ids{"a", "b", "c"}));

  model.MoveProfile(0, 10);
  EXPECT_EQ(ProfileIds(model.profiles()), (Ids{"q", "r", "p"}));

  model.MoveProfile(1, 0);
  EXPECT_EQ(ProfileIds(model.profiles()), (Ids{"r", "q", "p"}));

  model.MoveProfile(3, 0);
  EXPECT_EQ(ProfileIds(model.profiles()), (Ids{"r", "q", "p"}));
}

// Profiles.

// an edit reports what the engine must apply: its proxy, its fingerprint, its
// history choice
TEST_F(BrowserModelTest, DetectsChanges) {
  using enum BrowserModel::ProfileChange;
  using Changes = BrowserModel::ProfileChanges;
  BrowserModel& model = Start({MakeProfile("p")});
  ProxyConfig proxy;
  proxy.type = ProxyType::kHttp;
  proxy.host = "proxy.example";
  proxy.port = 8080;

  EXPECT_EQ(model.UpdateProfile(
                "p", [](BrowserProfile& profile) { profile.name = "Renamed"; }),
            Changes());
  EXPECT_EQ(model.UpdateProfile(
                "p", [&](BrowserProfile& profile) { profile.proxy = proxy; }),
            Changes({kProxy}));
  EXPECT_EQ(model.UpdateProfile(
                "p", [&](BrowserProfile& profile) { profile.proxy = proxy; }),
            Changes());
  EXPECT_EQ(model.UpdateProfile("p",
                                [](BrowserProfile& profile) {
                                  profile.fingerprint =
                                      FingerprintPreset(Platform::kWindows);
                                }),
            Changes({kFingerprint}));
  EXPECT_EQ(model.UpdateProfile("p",
                                [](BrowserProfile& profile) {
                                  profile.proxy = std::nullopt;
                                  profile.fingerprint = std::nullopt;
                                }),
            Changes({kProxy, kFingerprint}));
  EXPECT_EQ(
      model.UpdateProfile(
          "p", [](BrowserProfile& profile) { profile.SetSavesHistory(false); }),
      Changes({kHistory}));
  EXPECT_EQ(
      model.UpdateProfile(
          "p", [](BrowserProfile& profile) { profile.SetSavesHistory(false); }),
      Changes());
  EXPECT_EQ(
      model.UpdateProfile(
          "none", [&](BrowserProfile& profile) { profile.proxy = proxy; }),
      Changes());
  EXPECT_EQ(model.FindProfile("p")->name, "Renamed");
}

// a clear forgets the profile's closed tabs, and no other's
TEST_F(BrowserModelTest, ClearForgetsClosedTabs) {
  BrowserModel& model =
      Start({MakeProfile("a", {"one"}), MakeProfile("b", {"two"})});
  model.RemoveTab("one");
  model.RemoveTab("two");

  model.ForgetClosedTabs("a");

  const std::optional<ClosedTab> reopened = model.PopClosedTab();
  ASSERT_TRUE(reopened);
  EXPECT_EQ(reopened->profile_id, "b");
  EXPECT_EQ(model.PopClosedTab(), std::nullopt);
}

// a new profile, off from the start, has nothing to clear
TEST_F(BrowserModelTest, NewProfileOwesNoClear) {
  BrowserModel& model = Start();
  const std::string id = model.CreateProfile(
      [](BrowserProfile& profile) { profile.SetSavesHistory(false); });

  ASSERT_TRUE(model.FindProfile(id));
  EXPECT_FALSE(model.FindProfile(id)->saves_history);
  EXPECT_FALSE(model.FindProfile(id)->clears_history);
}

// profiles take the clock's time, created and updated
TEST_F(BrowserModelTest, Timestamps) {
  BrowserModel& model = Start();

  const std::string id =
      model.CreateProfile("Work", ProfileColor::kRose, "Clients");
  const BrowserProfile* created = model.FindProfile(id);

  ASSERT_TRUE(created);
  EXPECT_EQ(created->created_at, "2027-01-15T08:00:00.000Z");
  EXPECT_EQ(created->updated_at, created->created_at);
  EXPECT_EQ(created->name, "Work");
  EXPECT_EQ(created->color, ProfileColor::kRose);
  EXPECT_EQ(created->group, "Clients");

  clock_.Advance(base::Milliseconds(1500));
  model.UpdateProfile(
      id, [](BrowserProfile& profile) { profile.keep_loaded = true; });

  EXPECT_EQ(model.FindProfile(id)->created_at, "2027-01-15T08:00:00.000Z");
  EXPECT_EQ(model.FindProfile(id)->updated_at, "2027-01-15T08:00:01.500Z");
}

// Saving.

// a burst of changes saves once, after the delay, the profiles alone
TEST_F(BrowserModelTest, DebouncesSave) {
  BrowserModel& model = Start({MakeProfile("p", {"a"})});

  model.UpdateTab("a", [](Tab& tab) { tab.url = "https://moved.example/"; });
  model.UpdateTab("a", [](Tab& tab) { tab.title = "Moved"; });
  model.InsertTab({.id = "b"}, "p");

  // One wait, the last change's: each change put the save off again.
  EXPECT_TRUE(saves_.empty());
  EXPECT_EQ(Waiting(), 1u);
  EXPECT_EQ(task_environment_.NextMainThreadPendingTaskDelay(),
            BrowserModel::kSaveDelay);

  Fire();

  ASSERT_EQ(saves_.size(), 1u);
  ASSERT_TRUE(saves_[0].profiles);
  Ids titles;
  for (const Tab& tab : saves_[0].profiles->front().tabs) {
    titles.push_back(tab.title);
  }
  EXPECT_EQ(titles, (Ids{"Moved", ""}));
  EXPECT_EQ(saves_[0].settings, std::nullopt);
}

// Not in PaneKit, whose clock ran its waits by hand: each change puts the save
// off again, to the delay after the last.
TEST_F(BrowserModelTest, EachChangePutsTheSaveOff) {
  BrowserModel& model = Start({MakeProfile("p", {"a"})});

  model.UpdateTab("a", [](Tab& tab) { tab.title = "One"; });
  task_environment_.FastForwardBy(base::Milliseconds(200));
  model.UpdateTab("a", [](Tab& tab) { tab.title = "Two"; });
  task_environment_.FastForwardBy(base::Milliseconds(299));

  EXPECT_TRUE(saves_.empty());

  task_environment_.FastForwardBy(base::Milliseconds(1));

  ASSERT_EQ(saves_.size(), 1u);
  ASSERT_TRUE(saves_[0].profiles);
  EXPECT_EQ(saves_[0].profiles->front().tabs.front().title, "Two");
}

// a settings change saves the settings alone, and only when they change
TEST_F(BrowserModelTest, SavesSettings) {
  BrowserModel& model = Start({MakeProfile("p")});

  model.ChangeSettings(
      [](AppSettings& settings) { settings.theme = AppSettings().theme; });
  EXPECT_EQ(Waiting(), 0u);

  model.ChangeSettings(
      [](AppSettings& settings) { settings.theme = Theme::kDark; });
  Fire();

  ASSERT_EQ(saves_.size(), 1u);
  ASSERT_TRUE(saves_[0].settings);
  EXPECT_EQ(saves_[0].settings->theme, Theme::kDark);
  EXPECT_EQ(saves_[0].profiles, std::nullopt);
}

// a page reporting what the tab already holds saves nothing
TEST_F(BrowserModelTest, UnchangedTabSavesNothing) {
  BrowserModel& model = Start({MakeProfile("p", {"a"})});

  model.UpdateTab("a", [](Tab& tab) { tab.title = "A"; });

  EXPECT_EQ(Waiting(), 0u);
}

// saveNow writes what changed at once, and nothing when nothing did
TEST_F(BrowserModelTest, SavesNow) {
  BrowserModel& model = Start({MakeProfile("p")});

  model.SaveNow();
  EXPECT_TRUE(saves_.empty());

  model.SetExpanded("p", true);
  model.SaveNow();
  // Expansion is the session's: nothing to write.
  EXPECT_TRUE(saves_.empty());

  model.CreateProfile("New", ProfileColor::kBlue, std::nullopt);
  model.ChangeSettings(
      [](AppSettings& settings) { settings.sidebar_width = 300; });
  model.SaveNow();

  ASSERT_EQ(saves_.size(), 1u);
  ASSERT_TRUE(saves_[0].profiles);
  EXPECT_EQ(saves_[0].profiles->size(), 2u);
  ASSERT_TRUE(saves_[0].settings);
  EXPECT_EQ(saves_[0].settings->sidebar_width, 300);
  // The wait it replaced is gone.
  EXPECT_EQ(Waiting(), 0u);

  Fire();
  EXPECT_EQ(saves_.size(), 1u);
}

// a failed write asks again, and the next save writes that file
TEST_F(BrowserModelTest, RetriesFailedWrite) {
  BrowserModel& model = Start({MakeProfile("p")});

  model.ScheduleSave({BrowserModel::File::kProfiles});
  Fire();

  ASSERT_EQ(saves_.size(), 1u);
  ASSERT_TRUE(saves_[0].profiles);
  EXPECT_EQ(ProfileIds(*saves_[0].profiles), Ids{"p"});
}

// Observers.

// Not in PaneKit, where @Observable told SwiftUI of each change itself: each
// call tells the observers what it changed, in the header's order, once its
// changes are all made, and what it left as it was it does not tell.
TEST_F(BrowserModelTest, ObserversHearWhatChanged) {
  BrowserModel& model =
      Start({MakeProfile("p", {"a"}), MakeProfile("q", {"b"})});
  Events events(model);

  model.UpdateTab("a", [](Tab& tab) { tab.title = "A"; });
  model.ChangeSettings([](AppSettings&) {});
  model.SetExpanded("p", false);
  model.SetProxyStatus("p", std::nullopt);
  model.SetPage(BrowserModel::Page::kBrowser);
  model.SetActiveProfile(std::nullopt);
  model.SelectTab(std::nullopt);
  EXPECT_EQ(events.Take(), Ids{});

  model.UpdateTab("a", [](Tab& tab) { tab.title = "Moved"; });
  model.ChangeSettings(
      [](AppSettings& settings) { settings.theme = Theme::kDark; });
  model.SetExpanded("p", true);
  model.SetProxyStatus("p", BrowserModel::ProxyStatus::kTesting);
  model.SetProxyStatus("p", BrowserModel::ProxyStatus::kConnected);
  model.SetPage(BrowserModel::Page::kSettings);
  EXPECT_EQ(events.Take(), (Ids{"profiles", "settings", "expanded",
                                "proxy status", "proxy status", "page"}));

  // A tab of another profile: the tab, then its profile, both already set.
  model.SelectTab("a");
  model.SelectTab("b");
  EXPECT_EQ(events.Take(), (Ids{"tab, a in p", "profile, a in p", "tab, b in q",
                                "profile, b in q"}));

  // The same tab again changes nothing.
  model.SelectTab("b");
  EXPECT_EQ(events.Take(), Ids{});

  // A profile going takes what it held, each told once the profile is gone.
  model.SetExpanded("q", true);
  model.SetProxyStatus("q", BrowserModel::ProxyStatus::kFailed);
  events.Take();
  model.RemoveProfile("q");
  EXPECT_EQ(events.Take(), (Ids{"profiles", "expanded", "proxy status",
                                "tab, none in none", "profile, none in none"}));

  model.Load({}, false);
  EXPECT_EQ(events.Take(), Ids{"profiles"});
}

}  // namespace
}  // namespace pane::model
