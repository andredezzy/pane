// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_TAB_HISTORY_H_
#define PANE_MODEL_TAB_HISTORY_H_

#include <optional>
#include <string>
#include <vector>

namespace pane::model {

struct ClosedTab {
  friend bool operator==(const ClosedTab&, const ClosedTab&) = default;

  std::string url;
  std::string profile_id;
};

// Which tabs were used most recently, across every profile, and which were
// closed (for Reopen closed tab). Session state: not persisted, like the
// Electron app's tab store.
class TabHistory {
 public:
  static constexpr size_t kMaxClosedTabs = 20;

  TabHistory();
  TabHistory(const TabHistory&);
  TabHistory& operator=(const TabHistory&);
  ~TabHistory();

  void Use(const std::string& tab_id);
  void Forget(const std::string& tab_id);
  void PushClosed(ClosedTab tab);
  std::optional<ClosedTab> PopClosed();
  void ForgetClosed(const std::string& profile_id);

  // Most recent first.
  const std::vector<std::string>& mru() const { return mru_; }
  // Most recently closed first, at most kMaxClosedTabs.
  const std::vector<ClosedTab>& closed() const { return closed_; }

 private:
  std::vector<std::string> mru_;
  std::vector<ClosedTab> closed_;
};

// The most recently used of `tab_ids` still present, falling back to the
// last one when none of them appear in the history.
std::optional<std::string> MostRecentTab(
    const std::vector<std::string>& tab_ids,
    const std::vector<std::string>& history);

}  // namespace pane::model

#endif  // PANE_MODEL_TAB_HISTORY_H_
