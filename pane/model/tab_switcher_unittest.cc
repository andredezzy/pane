// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/tab_switcher.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's TabSwitcherTests (the Electron app's
// tab-switcher-grouping.test.ts).

Tab MakeTab(const std::string& id, std::optional<std::string> title = {}) {
  return {.id = id, .title = title.value_or(id)};
}

BrowserProfile MakeProfile(const std::string& id,
                           const std::string& name,
                           ProfileColor color,
                           std::vector<Tab> tabs) {
  BrowserProfile profile;
  profile.id = id;
  profile.name = name;
  profile.color = color;
  profile.tabs = std::move(tabs);
  return profile;
}

std::vector<BrowserProfile> Profiles() {
  return {MakeProfile("work", "Work", ProfileColor::kBlue,
                      {MakeTab("w1", "Gmail"), MakeTab("w2", "Caixa")}),
          MakeProfile("personal", "Personal", ProfileColor::kRose,
                      {MakeTab("p1", "Proton"), MakeTab("p2", "Inbox")})};
}

std::vector<std::string> GroupIds(const std::vector<SwitcherGroup>& groups) {
  std::vector<std::string> ids;
  for (const SwitcherGroup& group : groups) {
    ids.push_back(group.id);
  }
  return ids;
}

std::vector<std::string> TabIds(const SwitcherGroup& group) {
  std::vector<std::string> ids;
  for (const SwitcherTab& tab : group.tabs) {
    ids.push_back(tab.id);
  }
  return ids;
}

using Ids = std::vector<std::string>;

// Orders profiles and their tabs by most-recently-used.
TEST(TabSwitcherGroupsTest, OrdersByRecency) {
  // personal's p2 is most recent: personal first; within personal, p2 then p1
  // (most recent first, which differs from the natural [p1, p2] order).
  const auto groups = SwitcherGroups({"p2", "w1", "p1"}, Profiles(), 8);
  EXPECT_EQ(GroupIds(groups), (Ids{"personal", "work"}));
  EXPECT_EQ(TabIds(groups[0]), (Ids{"p2", "p1"}));
  EXPECT_EQ(TabIds(groups[1]), (Ids{"w1"}));
}

// Caps the total number of tabs across groups.
TEST(TabSwitcherGroupsTest, CapsTotal) {
  const auto groups = SwitcherGroups({"w1", "p1", "w2", "p2"}, Profiles(), 2);
  EXPECT_EQ(groups[0].tabs.size() + groups[1].tabs.size(), 2u);
  EXPECT_EQ(GroupIds(groups), (Ids{"work", "personal"}));
  EXPECT_EQ(TabIds(groups[0]), (Ids{"w1"}));
  EXPECT_EQ(TabIds(groups[1]), (Ids{"p1"}));
}

// Skips tab ids that no profile owns.
TEST(TabSwitcherGroupsTest, SkipsUnowned) {
  const auto groups = SwitcherGroups({"ghost", "w1"}, Profiles(), 8);
  EXPECT_EQ(GroupIds(groups), (Ids{"work"}));
  EXPECT_EQ(TabIds(groups[0]), (Ids{"w1"}));
}

// Falls back to a loading title and leaves an absent favicon nothing.
TEST(TabSwitcherGroupsTest, LoadingTitle) {
  const auto groups = SwitcherGroups(
      {"x1"}, {MakeProfile("x", "X", ProfileColor::kTeal, {MakeTab("x1", "")})},
      8);
  EXPECT_EQ(groups[0].tabs[0].title, "Loading...");
  EXPECT_EQ(groups[0].tabs[0].favicon, std::nullopt);
}

// Orders tabs within a profile by most-recently-used.
TEST(TabSwitcherGroupsTest, OrdersWithinProfile) {
  // Used w2 then w1: the group lists them most recent first, [w2, w1].
  const auto groups = SwitcherGroups({"w2", "w1"}, Profiles(), 8);
  EXPECT_EQ(GroupIds(groups), (Ids{"work"}));
  EXPECT_EQ(TabIds(groups[0]), (Ids{"w2", "w1"}));
}

std::vector<SwitcherTab> Flattened() {
  return {{.id = "a", .title = "a"},
          {.id = "b", .title = "b"},
          {.id = "c", .title = "c"}};
}

// Selects the previous tab's flattened position.
TEST(InitialSelectedIndexTest, PreviousTab) {
  EXPECT_EQ(SwitcherInitialSelectedIndex(Flattened(), "c"), 2);
}

// Falls back to the second item when the previous tab is gone.
TEST(InitialSelectedIndexTest, PreviousGone) {
  EXPECT_EQ(SwitcherInitialSelectedIndex(Flattened(), "missing"), 1);
}

// Falls back to 0 when only one tab is present.
TEST(InitialSelectedIndexTest, SingleTab) {
  EXPECT_EQ(SwitcherInitialSelectedIndex({{.id = "only", .title = "only"}},
                                         std::nullopt),
            0);
}

// Returns 0 for an empty list.
TEST(InitialSelectedIndexTest, EmptyList) {
  EXPECT_EQ(SwitcherInitialSelectedIndex({}, std::nullopt), 0);
}

// Returns the running tab-count offset for each group.
TEST(GroupOffsetsTest, RunningOffsets) {
  const auto groups = SwitcherGroups({"w1", "w2", "p1"}, Profiles(), 8);
  EXPECT_EQ(groups[0].tabs.size(), 2u);
  EXPECT_EQ(groups[1].tabs.size(), 1u);
  EXPECT_EQ(SwitcherGroupOffsets(groups), (std::vector<int>{0, 2}));
}

// Returns an empty array for no groups.
TEST(GroupOffsetsTest, NoGroups) {
  EXPECT_EQ(SwitcherGroupOffsets({}), std::vector<int>());
}

// Advances within range.
TEST(CycleTest, Advances) {
  EXPECT_EQ(SwitcherCycle(0, 1, 3), 1);
}

// Wraps forward past the last index.
TEST(CycleTest, WrapsForward) {
  EXPECT_EQ(SwitcherCycle(2, 1, 3), 0);
}

// Wraps backward past the first index.
TEST(CycleTest, WrapsBackward) {
  EXPECT_EQ(SwitcherCycle(0, -1, 3), 2);
}

// Returns 0 for an empty list.
TEST(CycleTest, EmptyList) {
  EXPECT_EQ(SwitcherCycle(0, 1, 0), 0);
}

}  // namespace
}  // namespace pane::model
