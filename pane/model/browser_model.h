// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_BROWSER_MODEL_H_
#define PANE_MODEL_BROWSER_MODEL_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/containers/enum_set.h"
#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "pane/model/app_settings.h"
#include "pane/model/profile.h"
#include "pane/model/tab_history.h"

namespace base {
class Clock;
}  // namespace base

namespace pane::model {

// Everything the app shows: profiles and their tabs (persisted), settings
// (persisted), and the session's selection, expansion, page, and proxy tests.
// The views read it and hear of its changes; the controller's intents change
// it, as the user acts and the engine reports (ADR 0077). It knows no engine
// and no file: what to write goes to `persist`, and time comes from its clock
// and from its sequence's timer, so its rules run in tests. It lives on the UI
// thread.
//
// What a live page is (loaded, its progress, back and forward, crashed, in
// picture in picture, with DevTools open) is the engine's, read from the
// tab's WebContents: the Swift model's mirrors of it are not ported
// (docs/plans/views-migration.md, What never gets ported).
class BrowserModel {
 public:
  enum class Page { kBrowser, kSettings };

  enum class ProxyStatus { kTesting, kConnected, kFailed };

  // What a profile's edit changed that the engine has to apply.
  enum class ProfileChange { kProxy, kFingerprint, kHistory };
  using ProfileChanges = base::
      EnumSet<ProfileChange, ProfileChange::kProxy, ProfileChange::kHistory>;

  // The files the state is saved in.
  enum class File { kProfiles, kSettings };
  using Files = base::EnumSet<File, File::kProfiles, File::kSettings>;

  // What a save writes: a copy of the state of each file that changed,
  // nullopt for one that did not.
  struct Save {
    Save();
    Save(Save&&);
    Save& operator=(Save&&);
    ~Save();

    std::optional<std::vector<BrowserProfile>> profiles;
    std::optional<AppSettings> settings;
  };

  using PersistCallback = base::RepeatingCallback<void(Save)>;

  // What the views read, as SwiftUI read the Swift model's properties. Each
  // call tells its events once its changes are all made, in the order below.
  // An observer reads the model: it does not change it from an event, which
  // the list's reentrancy check refuses. The tabs' recent use and Reopen
  // closed tab's stack (history()) have no event: a command reads them when
  // it runs.
  class Observer : public base::CheckedObserver {
   public:
    // The profiles, or a tab in them: each call that saves them (a title
    // reported again saves nothing), and Load.
    virtual void OnProfilesChanged() {}
    // A settings change that changed something.
    virtual void OnSettingsChanged() {}
    // A profile opened or closed in the sidebar.
    virtual void OnExpandedProfilesChanged() {}
    // A proxied profile's connection test started, answered, or forgotten.
    virtual void OnProxyStatusChanged() {}
    // The page shown changed: Browser, Settings, or Extensions.
    virtual void OnPageChanged() {}
    // The selected tab, then the active profile: each one that changed, so a
    // tab selected in another profile tells both. The sleep clocks hear
    // either.
    virtual void OnActiveTabChanged() {}
    virtual void OnActiveProfileChanged() {}
  };

  // A change saves this long after the last one, so a burst (a page's URL,
  // then its title, then its favicon) writes once.
  static constexpr base::TimeDelta kSaveDelay = base::Milliseconds(300);

  // `clock` stamps the profiles; a save waits on the current sequence.
  BrowserModel(std::vector<BrowserProfile> profiles,
               AppSettings settings,
               const base::Clock* clock,
               PersistCallback persist);
  BrowserModel(const BrowserModel&) = delete;
  BrowserModel& operator=(const BrowserModel&) = delete;
  ~BrowserModel();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // The profiles, once the data key opens them (ADR 0113): the model starts
  // with none. `changed` saves them too, as when they come from an earlier
  // Pane's plain file.
  void Load(std::vector<BrowserProfile> profiles, bool changed);

  // Lookups. A pointer holds until the next change.

  const BrowserProfile* FindProfile(const std::string& id) const;
  std::optional<std::string> ProfileIdOfTab(const std::string& tab_id) const;
  const Tab* FindTab(const std::string& id) const;
  const Tab* ActiveTab() const;

  // Settings.

  // Saves only a change: the same value again writes nothing.
  void ChangeSettings(base::FunctionRef<void(AppSettings&)> change);

  // Profiles.

  std::string CreateProfile(std::string name,
                            ProfileColor color,
                            std::optional<std::string> group);
  // A new profile, at the end, as `configure` makes it; its id. Off from the
  // start, it has kept nothing to clear.
  std::string CreateProfile(base::FunctionRef<void(BrowserProfile&)> configure);
  // What the change did to the profile's proxy, fingerprint, and history
  // choice, for the engine to apply. `change` edits the profile alone.
  ProfileChanges UpdateProfile(const std::string& id,
                               base::FunctionRef<void(BrowserProfile&)> change);
  void RemoveProfile(const std::string& id);
  // `destination` past the end puts it last.
  void MoveProfile(int source, int destination);
  void SetExpanded(const std::string& id, bool expanded);
  void SetProxyStatus(const std::string& id, std::optional<ProxyStatus> status);

  // Tabs.

  // After the anchor tab, or last without one (or when it is not the
  // profile's).
  void InsertTab(Tab tab,
                 const std::string& profile_id,
                 const std::optional<std::string>& after = std::nullopt);
  // Saves only a change: a page reporting the same title writes nothing.
  // `change` edits the tab alone.
  void UpdateTab(const std::string& id, base::FunctionRef<void(Tab&)> change);
  // Removes the tab and remembers it for Reopen closed tab.
  std::optional<Tab> RemoveTab(const std::string& id);
  std::optional<ClosedTab> PopClosedTab();
  // What Reopen closed tab holds of a profile, which a clear of its history
  // takes, as Chrome's takes its recently closed tabs.
  void ForgetClosedTabs(const std::string& profile_id);
  // `destination` past the end puts it last.
  void MoveTab(const std::string& profile_id, int source, int destination);

  // Selection.

  // A tab makes its profile the active one, and the most recently used.
  void SelectTab(std::optional<std::string> tab_id);
  void SetActiveProfile(std::optional<std::string> id);
  void SetPage(Page page);

  // Persistence.

  // kSaveDelay after the last change, the files that changed are handed to
  // `persist`. A title, URL, or favicon change saves too, so the model hands
  // over a copy of the state, for the encoding and the write to run off the
  // UI thread. A write that failed asks again here.
  void ScheduleSave(Files files);
  // What changed, handed to `persist` at once: at quit, and for scenarios that
  // read the files.
  void SaveNow();

  const std::vector<BrowserProfile>& profiles() const { return profiles_; }
  const AppSettings& settings() const { return settings_; }
  const TabHistory& history() const { return history_; }
  const std::optional<std::string>& active_tab_id() const {
    return active_tab_id_;
  }
  const std::optional<std::string>& active_profile_id() const {
    return active_profile_id_;
  }
  const std::set<std::string>& expanded_profile_ids() const {
    return expanded_profile_ids_;
  }
  Page page() const { return page_; }
  const std::map<std::string, ProxyStatus>& proxy_status() const {
    return proxy_status_;
  }

 private:
  // A tab gone: its use forgotten, and its selection cleared.
  void ForgetTab(const std::string& id);
  // Tells the observers which of the selection's two changed since the call
  // began, when they were `tab_before` and `profile_before`.
  void NotifySelection(const std::optional<std::string>& tab_before,
                       const std::optional<std::string>& profile_before);
  // The clock's time as a profile's stamp: ISO 8601, in UTC, to the
  // millisecond.
  std::string Timestamp() const;

  const raw_ptr<const base::Clock> clock_;
  const PersistCallback persist_;

  std::vector<BrowserProfile> profiles_;
  AppSettings settings_;
  TabHistory history_;
  std::optional<std::string> active_tab_id_;
  std::optional<std::string> active_profile_id_;
  // Profiles open in the sidebar. Not persisted: collapsed on every launch.
  std::set<std::string> expanded_profile_ids_;
  Page page_ = Page::kBrowser;
  // Each proxied profile's last connection test (the Electron app's
  // proxy-status-store), for the sidebar's dot.
  std::map<std::string, ProxyStatus> proxy_status_;

  // What changed since the last save: only those files are written.
  Files changed_;
  base::OneShotTimer save_timer_;
  base::ObserverList<Observer> observers_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_BROWSER_MODEL_H_
