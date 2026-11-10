// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_UI_APP_H_
#define PANE_UI_APP_UI_APP_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "pane/app/data_launch.h"
#include "pane/browser/profile_contexts.h"
#include "pane/model/browser_model.h"
#include "pane/model/crash_loop.h"
#include "pane/model/toolbar_progress.h"
#include "pane/ui/tabs/favicons.h"
#include "pane/ui/window/window.h"
#include "pane/updates/updater.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}

namespace gfx {
class Image;
}

namespace pane {

class AppEvents;
class Automation;

namespace model {
struct AppSettings;
struct ProfileDraft;
}  // namespace model

// Pane's Views UI, as a launch of Chromium's loader starts it
// (StartViewsLaunch): the model, the window that shows it, and the engine's
// pages, joined as the Swift app's BrowserController joined them. The
// profiles and their tabs are the model's, loaded once the data key opens and
// saved as they change (ModelStore); a tab has a page (a WebContents in its
// profile's hidden Browser) once it is selected, and keeps its place while it
// has none. Behind the lock, and until the profiles have loaded and an
// earlier Pane's have moved in, nothing opens, loads, or shows.
class UiApp : public AppData::Client,
              public Window::Controller,
              public model::BrowserModel::Observer,
              public Favicons::Observer,
              public Updater::Observer {
 public:
  // Starts it, once the browser has started and the data opened (OpenData).
  static void Start();
  static UiApp* Get();

  UiApp(const UiApp&) = delete;
  UiApp& operator=(const UiApp&) = delete;
  ~UiApp() override;

  Window* window() { return window_.get(); }
  Updater& updater() { return *updater_; }
  model::BrowserModel& model();
  const model::BrowserModel& model() const;

  // What the user asks, from the window, the menus, and the channel alike
  // (the Swift's intents).

  // The profile sheet's Create: the profile as the form has it, expanded; its
  // id.
  std::string CreateProfile(const model::ProfileDraft& draft);
  // A new tab in the profile, selected: at `url`, or at the start page with
  // the address field taking the keys. Its id.
  std::string OpenTab(const std::string& profile_id, std::optional<GURL> url);
  // Selects a tab: its page shows at once, or is made after the frame that
  // answers the input when it has none.
  void Select(const std::string& tab_id) override;
  // A profile collapses, and its pages go; its tabs stay (CONTEXT.md:
  // Unload).
  void CollapseProfile(const std::string& profile_id);
  // The browser, or Settings: under Settings no tab is selected, and no
  // profile shows as active (ADR 0064).
  void ShowPage(model::BrowserModel::Page page);
  // A theme reaches the app, its menus, and its pages at once (ADR 0001).
  void ChangeSettings(base::FunctionRef<void(model::AppSettings&)> change);
  // What changed is written; `written` runs once it is on the disk.
  void SaveNow(base::OnceClosure written);
  // Saved, then closed, then the volume detaches (DataDidStop): `saved` runs
  // once what changed is on the disk, before the window closes.
  void Quit(base::OnceClosure saved);
  // Lock Pane (ADR 0116): the PIN screen at once, and behind it every page
  // closes, what changed is written, the profiles leave the model, each
  // closes in the engine, and the volume detaches.
  void LockNow();
  // What Pane is started with or handed opens as new tabs, in order, in the
  // profile in use (the first, made when there is none), once the lock is
  // open. The last one is selected.
  void OpenLaunchURLs(std::vector<GURL> urls);
  // The window shows again, in front: a new one if it was closed.
  void ShowWindow();
  // What another app handed Pane opens, its window shown again (ADR 0109).
  void OpenHandedURLs(std::vector<GURL> urls);

  // The tab's page, when it has one.
  content::WebContents* PageOf(const std::string& tab_id) const;
  // The profile in the engine, while it is open.
  Profile* ProfileIfOpen(const std::string& profile_id) const;

  // What shows, for the channel's replies, in the Swift app's shape.
  base::DictValue Snapshot() const;
  // Deleted profiles the engine hasn't confirmed yet.
  size_t pending_profile_deletions() const { return deleting_.size(); }

  // AppData::Client:
  void AccessDidOpen() override;
  void LockRequested() override;

  // Window::Controller:
  SidebarContent Sidebar() override;
  RootView::Shows Shows() override;
  void ToggleProfile(const std::string& profile_id) override;
  void CloseTab(const std::string& tab_id) override;
  void NewTabIn(const std::string& profile_id) override;
  void EditProfile(const std::string& profile_id) override;
  void SetKeepLoaded(const std::string& profile_id, bool keep_loaded) override;
  // The profile, its tabs, and its pages go now; its data once the engine
  // confirms (ADR 0020), recorded in deletions.json until then.
  void DeleteProfile(const std::string& profile_id) override;
  void CollapseAll() override;
  void NewProfile() override;
  void ToggleSettings() override;
  void DownloadUpdate() override;
  void DismissUpdate() override;
  void MoveProfile(int from, int to) override;
  void MoveTab(const std::string& profile_id, int from, int to) override;
  void ResizeSidebar(double width) override;
  void EndSidebarResize() override;
  void ResetSidebarWidth() override;
  void AddressEntered(const std::u16string& text) override;
  void AddressLeft() override;
  void Back() override;
  void Forward() override;
  // Reload, or a crashed tab's page made again (the sad tab's Reload).
  void Reload() override;
  void Stop() override;
  model::ToolbarProgress& LoadProgress() override;
  void NewTab() override;
  void SelectTabAt(int number) override;
  void TabInserted(content::WebContents* contents,
                   content::WebContents* opener,
                   bool foreground) override;
  void TabRemoved(content::WebContents* contents) override;
  void ActiveTabChanged(content::WebContents* contents) override;
  void WindowClosed() override;

  // model::BrowserModel::Observer:
  void OnProfilesChanged() override;
  void OnSettingsChanged() override;
  void OnExpandedProfilesChanged() override;
  void OnActiveTabChanged() override;
  void OnActiveProfileChanged() override;
  void OnProxyStatusChanged() override;
  void OnPageChanged() override;

  // Favicons::Observer:
  void OnFaviconChanged(const std::string& tab_id) override;

  // Updater::Observer:
  void OnUpdaterChanged() override;

 private:
  // A tab's page, as the model hears of it.
  class Page;

  UiApp();

  // Behind the lock, and until the data has opened, nothing runs.
  bool InteractionAllowed() const;

  // A tab whose page loads behind the selected one, after `anchor` or last.
  std::string OpenTabBehind(const std::string& profile_id,
                            const GURL& url,
                            const std::optional<std::string>& anchor);
  // The engine makes the tab's page, in its profile's Browser.
  void OpenPage(const std::string& tab_id, Profile* profile, bool foreground);
  // Gives `contents` the id its tab had before it existed.
  void AdoptOpeningTab(content::WebContents* contents);
  void AddPage(const std::string& tab_id, content::WebContents* contents);
  // The selected tab's page, in the window: never behind the lock.
  void Show(content::WebContents* contents);
  // The page goes, its tab stays (CONTEXT.md: Unload).
  void Release(const std::string& tab_id);
  void UnloadProfile(const std::string& profile_id);
  // The profile's most recent tab takes the place of the one that closed.
  void SelectAfterClosing(const std::optional<std::string>& profile_id);
  void ExpandProfile(const std::string& profile_id);
  // The tab goes from the model; when it was selected, its profile's most
  // recent tab takes its place.
  void RemoveTabAndReselect(const std::string& tab_id);
  // The engine answered a delete: the folder goes now, unless a lock came.
  void ProfileDeleted(const std::string& profile_id, bool removes_folder);
  // A crashed tab's page reloads, or is made again, its crash loop starting
  // over (the Swift's reloadCrashed).
  void ReloadCrashed(const std::string& tab_id);
  // How far the tab's page has loaded a new document in its main frame, or
  // nullopt while it doesn't (ADR 0081).
  std::optional<double> LoadingOf(const std::string& tab_id) const;
  // Another tab came forward, or the selected one's page went: the bar shows
  // its load as it stands, or hides at once.
  void ShowLoadOf(const std::optional<std::string>& tab_id);
  // The profile sheet (N1's), for New and Edit profile: the prototype's sheet
  // stands in.
  void PresentProfileSheet();

  // From a page.
  void PageTitleChanged(const std::string& tab_id);
  // Its favicon came: drawn at once, saved with the tab once encoded.
  void PageFaviconChanged(const std::string& tab_id, const gfx::Image& image);
  // It went somewhere: its history changed.
  void PageNavigated(const std::string& tab_id);
  // A new document started or stopped loading in its main frame, or went on.
  void PageLoadingChanged(const std::string& tab_id,
                          std::optional<double> from,
                          std::optional<double> to);
  void PageURLChanged(const std::string& tab_id);
  void PageDestroyed(const std::string& tab_id);
  // Its renderer died: the first crash reloads it, and a second within a
  // minute, or one behind another tab, leaves it crashed.
  void PageCrashed(const std::string& tab_id);
  // A page committed in place of one that died: the tab is back, and its
  // crash loop starts over.
  void PageCameBack(const std::string& tab_id);

  const raw_ref<AppData> data_;
  ProfileContexts contexts_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<AppEvents> events_;
  std::unique_ptr<Automation> automation_;
  // Each tab's page, by its id.
  std::map<std::string, std::unique_ptr<Page>> pages_;
  // The tab whose page the engine is making, while it inserts it.
  std::optional<std::string> opening_tab_id_;
  // A new tab with no page of the user's: its address field takes the keys
  // once it shows, to type in, rather than its page.
  std::optional<std::string> focuses_address_;
  // The tab that showed when Pane locked, shown again once it opens.
  std::optional<std::string> resume_tab_id_;
  // The URLs Pane was started with or handed, while they wait behind the
  // lock.
  std::vector<GURL> launch_urls_;
  // When each tab's renderer last died, to tell a crash loop from a crash.
  model::CrashLoop crashes_;
  // The tabs that show the sad tab's place until they reload: a page that
  // died and stayed dead, or one whose profile would not open.
  std::set<std::string> crashed_tab_ids_;
  // Each tab's favicon as the sidebar draws it.
  Favicons favicons_;
  // The selected tab's load, as the address's bar draws it (the Swift's
  // progressBar): one for the app, whichever window draws it.
  model::ToolbarProgress progress_;
  // The saved favicons were decoded ahead, since the profiles last loaded.
  bool favicons_warmed_ = false;
  // Profiles deleted whose engine data hasn't gone yet.
  std::set<std::string> deleting_;
  // The sidebar's width as it shows: a drag's, saved as it ends.
  double sidebar_width_ = 0;
  base::ScopedObservation<model::BrowserModel, model::BrowserModel::Observer>
      model_observation_{this};
  base::ScopedObservation<Favicons, Favicons::Observer> favicons_observation_{
      this};
  // The update check and download, which the pill reads.
  std::unique_ptr<Updater> updater_;
  base::ScopedObservation<Updater, Updater::Observer> updater_observation_{
      this};
  base::WeakPtrFactory<UiApp> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_APP_UI_APP_H_
