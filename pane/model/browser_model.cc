// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/browser_model.h"

#include <algorithm>
#include <utility>

#include "base/containers/map_util.h"
#include "base/location.h"
#include "base/strings/stringprintf.h"
#include "base/time/clock.h"
#include "base/types/optional_util.h"
#include "base/uuid.h"

namespace pane::model {

namespace {

// The item at `source` moved to `destination`, clamped to the list's ends;
// false, and nothing moved, when there is no item at `source`.
template <typename T>
bool MoveItem(std::vector<T>& items, int source, int destination) {
  if (source < 0 || static_cast<size_t>(source) >= items.size()) {
    return false;
  }
  T item = std::move(items[source]);
  items.erase(items.begin() + source);
  const int place = std::clamp(destination, 0, static_cast<int>(items.size()));
  items.insert(items.begin() + place, std::move(item));
  return true;
}

}  // namespace

BrowserModel::Save::Save() = default;
BrowserModel::Save::Save(Save&&) = default;
BrowserModel::Save& BrowserModel::Save::operator=(Save&&) = default;
BrowserModel::Save::~Save() = default;

BrowserModel::BrowserModel(std::vector<BrowserProfile> profiles,
                           AppSettings settings,
                           const base::Clock* clock,
                           PersistCallback persist)
    : clock_(clock),
      persist_(std::move(persist)),
      profiles_(std::move(profiles)),
      settings_(std::move(settings)) {}

BrowserModel::~BrowserModel() = default;

void BrowserModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void BrowserModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void BrowserModel::Load(std::vector<BrowserProfile> profiles, bool changed) {
  profiles_ = std::move(profiles);
  if (changed) {
    ScheduleSave({File::kProfiles});
  }
  observers_.Notify(&Observer::OnProfilesChanged);
}

const BrowserProfile* BrowserModel::FindProfile(const std::string& id) const {
  const auto profile = std::ranges::find(profiles_, id, &BrowserProfile::id);
  return profile == profiles_.end() ? nullptr : &*profile;
}

std::optional<std::string> BrowserModel::ProfileIdOfTab(
    const std::string& tab_id) const {
  for (const BrowserProfile& profile : profiles_) {
    if (std::ranges::contains(profile.tabs, tab_id, &Tab::id)) {
      return profile.id;
    }
  }
  return std::nullopt;
}

const Tab* BrowserModel::FindTab(const std::string& id) const {
  for (const BrowserProfile& profile : profiles_) {
    const auto tab = std::ranges::find(profile.tabs, id, &Tab::id);
    if (tab != profile.tabs.end()) {
      return &*tab;
    }
  }
  return nullptr;
}

const Tab* BrowserModel::ActiveTab() const {
  return active_tab_id_ ? FindTab(*active_tab_id_) : nullptr;
}

void BrowserModel::ChangeSettings(
    base::FunctionRef<void(AppSettings&)> change) {
  AppSettings changed = settings_;
  change(changed);
  if (changed == settings_) {
    return;
  }
  settings_ = std::move(changed);
  ScheduleSave({File::kSettings});
  observers_.Notify(&Observer::OnSettingsChanged);
}

std::string BrowserModel::CreateProfile(std::string name,
                                        ProfileColor color,
                                        std::optional<std::string> group) {
  return CreateProfile([&](BrowserProfile& profile) {
    profile.name = std::move(name);
    profile.color = color;
    profile.group = std::move(group);
  });
}

std::string BrowserModel::CreateProfile(
    base::FunctionRef<void(BrowserProfile&)> configure) {
  BrowserProfile profile;
  profile.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  profile.created_at = Timestamp();
  profile.updated_at = profile.created_at;
  configure(profile);
  profile.clears_history = false;
  std::string id = profile.id;
  profiles_.push_back(std::move(profile));
  ScheduleSave({File::kProfiles});
  observers_.Notify(&Observer::OnProfilesChanged);
  return id;
}

BrowserModel::ProfileChanges BrowserModel::UpdateProfile(
    const std::string& id,
    base::FunctionRef<void(BrowserProfile&)> change) {
  const auto profile = std::ranges::find(profiles_, id, &BrowserProfile::id);
  if (profile == profiles_.end()) {
    return {};
  }

  const std::optional<ProxyConfig> proxy = profile->proxy;
  const std::optional<Fingerprint> fingerprint = profile->fingerprint;
  const bool saves_history = profile->saves_history;
  change(*profile);
  profile->updated_at = Timestamp();

  ProfileChanges changes;
  changes.PutOrRemove(ProfileChange::kProxy, profile->proxy != proxy);
  changes.PutOrRemove(ProfileChange::kFingerprint,
                      profile->fingerprint != fingerprint);
  changes.PutOrRemove(ProfileChange::kHistory,
                      profile->saves_history != saves_history);
  ScheduleSave({File::kProfiles});
  observers_.Notify(&Observer::OnProfilesChanged);
  return changes;
}

void BrowserModel::RemoveProfile(const std::string& id) {
  const BrowserProfile* profile = FindProfile(id);
  if (!profile) {
    return;
  }

  // A copy: `id` may be the model's own (a profile's, the selection's), which
  // the removal moves or destroys.
  const std::string removed = id;
  const std::optional<std::string> tab_before = active_tab_id_;
  const std::optional<std::string> profile_before = active_profile_id_;
  for (const Tab& tab : profile->tabs) {
    ForgetTab(tab.id);
  }
  std::erase_if(profiles_,
                [&](const BrowserProfile& each) { return each.id == removed; });
  const bool collapsed = expanded_profile_ids_.erase(removed) > 0;
  const bool status_forgotten = proxy_status_.erase(removed) > 0;
  if (active_profile_id_ == removed) {
    active_profile_id_ = std::nullopt;
    active_tab_id_ = std::nullopt;
  }
  ScheduleSave({File::kProfiles});

  observers_.Notify(&Observer::OnProfilesChanged);
  if (collapsed) {
    observers_.Notify(&Observer::OnExpandedProfilesChanged);
  }
  if (status_forgotten) {
    observers_.Notify(&Observer::OnProxyStatusChanged);
  }
  NotifySelection(tab_before, profile_before);
}

void BrowserModel::MoveProfile(int source, int destination) {
  if (!MoveItem(profiles_, source, destination)) {
    return;
  }
  ScheduleSave({File::kProfiles});
  observers_.Notify(&Observer::OnProfilesChanged);
}

void BrowserModel::SetExpanded(const std::string& id, bool expanded) {
  const bool changed = expanded ? expanded_profile_ids_.insert(id).second
                                : expanded_profile_ids_.erase(id) > 0;
  if (changed) {
    observers_.Notify(&Observer::OnExpandedProfilesChanged);
  }
}

void BrowserModel::SetProxyStatus(const std::string& id,
                                  std::optional<ProxyStatus> status) {
  if (status == base::OptionalFromPtr(base::FindOrNull(proxy_status_, id))) {
    return;
  }
  if (status) {
    proxy_status_[id] = *status;
  } else {
    proxy_status_.erase(id);
  }
  observers_.Notify(&Observer::OnProxyStatusChanged);
}

void BrowserModel::InsertTab(Tab tab,
                             const std::string& profile_id,
                             const std::optional<std::string>& after) {
  const auto profile =
      std::ranges::find(profiles_, profile_id, &BrowserProfile::id);
  if (profile == profiles_.end()) {
    return;
  }

  std::vector<Tab>& tabs = profile->tabs;
  auto position = tabs.end();
  if (after) {
    const auto anchor = std::ranges::find(tabs, *after, &Tab::id);
    if (anchor != tabs.end()) {
      position = anchor + 1;
    }
  }
  tabs.insert(position, std::move(tab));
  ScheduleSave({File::kProfiles});
  observers_.Notify(&Observer::OnProfilesChanged);
}

void BrowserModel::UpdateTab(const std::string& id,
                             base::FunctionRef<void(Tab&)> change) {
  for (BrowserProfile& profile : profiles_) {
    const auto tab = std::ranges::find(profile.tabs, id, &Tab::id);
    if (tab == profile.tabs.end()) {
      continue;
    }
    const Tab before = *tab;
    change(*tab);
    if (*tab != before) {
      ScheduleSave({File::kProfiles});
      observers_.Notify(&Observer::OnProfilesChanged);
    }
    return;
  }
}

std::optional<Tab> BrowserModel::RemoveTab(const std::string& id) {
  for (BrowserProfile& profile : profiles_) {
    const auto tab = std::ranges::find(profile.tabs, id, &Tab::id);
    if (tab == profile.tabs.end()) {
      continue;
    }

    const std::optional<std::string> tab_before = active_tab_id_;
    const std::optional<std::string> profile_before = active_profile_id_;
    // By the removed tab's own id from here: `id` may be the tab's, which
    // the removal moves.
    Tab removed = std::move(*tab);
    profile.tabs.erase(tab);
    history_.PushClosed({.url = removed.url, .profile_id = profile.id});
    ForgetTab(removed.id);
    ScheduleSave({File::kProfiles});

    observers_.Notify(&Observer::OnProfilesChanged);
    NotifySelection(tab_before, profile_before);
    return removed;
  }
  return std::nullopt;
}

std::optional<ClosedTab> BrowserModel::PopClosedTab() {
  return history_.PopClosed();
}

void BrowserModel::ForgetClosedTabs(const std::string& profile_id) {
  history_.ForgetClosed(profile_id);
}

void BrowserModel::MoveTab(const std::string& profile_id,
                           int source,
                           int destination) {
  const auto profile =
      std::ranges::find(profiles_, profile_id, &BrowserProfile::id);
  if (profile == profiles_.end() ||
      !MoveItem(profile->tabs, source, destination)) {
    return;
  }
  ScheduleSave({File::kProfiles});
  observers_.Notify(&Observer::OnProfilesChanged);
}

void BrowserModel::SelectTab(std::optional<std::string> tab_id) {
  const std::optional<std::string> tab_before = active_tab_id_;
  const std::optional<std::string> profile_before = active_profile_id_;
  active_tab_id_ = std::move(tab_id);
  if (active_tab_id_) {
    active_profile_id_ = ProfileIdOfTab(*active_tab_id_);
    history_.Use(*active_tab_id_);
  }
  NotifySelection(tab_before, profile_before);
}

void BrowserModel::SetActiveProfile(std::optional<std::string> id) {
  const std::optional<std::string> tab_before = active_tab_id_;
  const std::optional<std::string> profile_before = active_profile_id_;
  active_profile_id_ = std::move(id);
  NotifySelection(tab_before, profile_before);
}

void BrowserModel::SetPage(Page page) {
  if (page == page_) {
    return;
  }
  page_ = page;
  observers_.Notify(&Observer::OnPageChanged);
}

void BrowserModel::ScheduleSave(Files files) {
  changed_.PutAll(files);
  save_timer_.Start(FROM_HERE, kSaveDelay, this, &BrowserModel::SaveNow);
}

void BrowserModel::SaveNow() {
  save_timer_.Stop();
  if (changed_.empty()) {
    return;
  }

  Save save;
  if (changed_.Has(File::kProfiles)) {
    save.profiles = profiles_;
  }
  if (changed_.Has(File::kSettings)) {
    save.settings = settings_;
  }
  changed_.Clear();
  persist_.Run(std::move(save));
}

void BrowserModel::ForgetTab(const std::string& id) {
  history_.Forget(id);
  if (active_tab_id_ == id) {
    active_tab_id_ = std::nullopt;
  }
}

void BrowserModel::NotifySelection(
    const std::optional<std::string>& tab_before,
    const std::optional<std::string>& profile_before) {
  const bool tab_changed = active_tab_id_ != tab_before;
  const bool profile_changed = active_profile_id_ != profile_before;
  if (tab_changed) {
    observers_.Notify(&Observer::OnActiveTabChanged);
  }
  if (profile_changed) {
    observers_.Notify(&Observer::OnActiveProfileChanged);
  }
}

// As Foundation's ISO8601DateFormatter wrote it with fractional seconds, and
// as Chromium's TimeFormatAsIso8601 (//base:i18n's) writes it: the
// millisecond truncated.
std::string BrowserModel::Timestamp() const {
  base::Time::Exploded exploded;
  clock_->Now().UTCExplode(&exploded);
  return base::StringPrintf(
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", exploded.year, exploded.month,
      exploded.day_of_month, exploded.hour, exploded.minute, exploded.second,
      exploded.millisecond);
}

}  // namespace pane::model
