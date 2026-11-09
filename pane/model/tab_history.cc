// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/tab_history.h"

#include <algorithm>
#include <utility>

namespace pane::model {

TabHistory::TabHistory() = default;
TabHistory::TabHistory(const TabHistory&) = default;
TabHistory& TabHistory::operator=(const TabHistory&) = default;
TabHistory::~TabHistory() = default;

void TabHistory::Use(const std::string& tab_id) {
  Forget(tab_id);
  mru_.insert(mru_.begin(), tab_id);
}

void TabHistory::Forget(const std::string& tab_id) {
  std::erase(mru_, tab_id);
}

void TabHistory::PushClosed(ClosedTab tab) {
  closed_.insert(closed_.begin(), std::move(tab));
  if (closed_.size() > kMaxClosedTabs) {
    closed_.resize(kMaxClosedTabs);
  }
}

std::optional<ClosedTab> TabHistory::PopClosed() {
  if (closed_.empty()) {
    return std::nullopt;
  }
  ClosedTab tab = std::move(closed_.front());
  closed_.erase(closed_.begin());
  return tab;
}

void TabHistory::ForgetClosed(const std::string& profile_id) {
  std::erase_if(closed_, [&](const ClosedTab& tab) {
    return tab.profile_id == profile_id;
  });
}

std::optional<std::string> MostRecentTab(
    const std::vector<std::string>& tab_ids,
    const std::vector<std::string>& history) {
  for (const std::string& tab_id : history) {
    if (std::ranges::find(tab_ids, tab_id) != tab_ids.end()) {
      return tab_id;
    }
  }
  return tab_ids.empty() ? std::nullopt : std::make_optional(tab_ids.back());
}

}  // namespace pane::model
