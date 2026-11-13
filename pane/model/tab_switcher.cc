// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/tab_switcher.h"

#include <algorithm>
#include <map>

namespace pane::model {

SwitcherGroup::SwitcherGroup() = default;
SwitcherGroup::SwitcherGroup(const SwitcherGroup&) = default;
SwitcherGroup& SwitcherGroup::operator=(const SwitcherGroup&) = default;
SwitcherGroup::~SwitcherGroup() = default;

std::vector<SwitcherGroup> SwitcherGroups(
    const std::vector<std::string>& mru,
    const std::vector<BrowserProfile>& profiles,
    int max_tabs) {
  std::map<std::string, std::pair<const BrowserProfile*, const Tab*>> owners;
  for (const BrowserProfile& profile : profiles) {
    for (const Tab& tab : profile.tabs) {
      owners[tab.id] = {&profile, &tab};
    }
  }
  std::vector<SwitcherGroup> groups;
  std::map<std::string, size_t> index_by_profile;
  int count = 0;
  for (const std::string& tab_id : mru) {
    if (count >= max_tabs) {
      break;
    }
    const auto owner = owners.find(tab_id);
    if (owner == owners.end()) {
      continue;
    }
    const auto& [profile, tab] = owner->second;
    auto [index, added] =
        index_by_profile.try_emplace(profile->id, groups.size());
    if (added) {
      SwitcherGroup group;
      group.id = profile->id;
      group.name = profile->name;
      group.color = profile->color;
      groups.push_back(std::move(group));
    }
    groups[index->second].tabs.push_back(
        {.id = tab->id,
         .title = tab->title.empty() ? "Loading..." : tab->title,
         .favicon = tab->favicon.empty() ? std::nullopt
                                         : std::make_optional(tab->favicon)});
    ++count;
  }
  return groups;
}

int SwitcherInitialSelectedIndex(const std::vector<SwitcherTab>& flattened,
                                 const std::optional<std::string>& previous) {
  if (flattened.empty()) {
    return 0;
  }
  const auto it = std::ranges::find_if(flattened, [&](const SwitcherTab& tab) {
    return previous && tab.id == *previous;
  });
  return it != flattened.end()
             ? static_cast<int>(it - flattened.begin())
             : std::min(1, static_cast<int>(flattened.size()) - 1);
}

int SwitcherCycle(int current, int delta, int count) {
  return count == 0 ? 0 : (current + delta + count) % count;
}

}  // namespace pane::model
