// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_TAB_SWITCHER_H_
#define PANE_MODEL_TAB_SWITCHER_H_

#include <optional>
#include <string>
#include <vector>

#include "pane/model/profile.h"

namespace pane::model {

// The Ctrl+Tab switcher's model: the most recently used tabs, grouped under
// their profile. Group order and tab order within a group both follow
// recency; the total is capped so the switcher stays a recent-tabs list.

struct SwitcherTab {
  friend bool operator==(const SwitcherTab&, const SwitcherTab&) = default;

  std::string id;
  std::string title;
  std::optional<std::string> favicon;
};

struct SwitcherGroup {
  SwitcherGroup();
  SwitcherGroup(const SwitcherGroup&);
  SwitcherGroup& operator=(const SwitcherGroup&);
  ~SwitcherGroup();

  std::string id;
  std::string name;
  ProfileColor color = ProfileColor::kBlue;
  std::vector<SwitcherTab> tabs;
};

inline constexpr int kSwitcherMaxVisibleTabs = 8;

std::vector<SwitcherGroup> SwitcherGroups(
    const std::vector<std::string>& mru,
    const std::vector<BrowserProfile>& profiles,
    int max_tabs = kSwitcherMaxVisibleTabs);

// Where selection starts: the previous tab, so a quick flick of Ctrl+Tab goes
// back to it; else the second item.
int SwitcherInitialSelectedIndex(const std::vector<SwitcherTab>& flattened,
                                 const std::optional<std::string>& previous);

// Each group's offset into the flattened tab list.
std::vector<int> SwitcherGroupOffsets(const std::vector<SwitcherGroup>& groups);

int SwitcherCycle(int current, int delta, int count);

}  // namespace pane::model

#endif  // PANE_MODEL_TAB_SWITCHER_H_
