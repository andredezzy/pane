// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_UI_APP_H_
#define PANE_UI_APP_UI_APP_H_

#include <cstdint>
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
#include "pane/browser/profile_network.h"
#include "pane/model/browser_model.h"
#include "pane/model/crash_loop.h"
#include "pane/model/tab_switcher.h"
#include "pane/model/toolbar_progress.h"
#include "pane/ui/app/commands.h"
#include "pane/ui/app/lock_triggers.h"
#include "pane/ui/app/menu_bar.h"
#include "pane/ui/app/sleep_controller.h"
#include "pane/ui/dialogs/page_dialogs.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/lock/lock_screen_view.h"
#include "pane/ui/page/page_context_menu.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/tabs/favicons.h"
#include "pane/ui/tabs/tab_switcher_view.h"
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
class AppIconImages;
class Automation;
class DockIcon;

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
              public ProfileContexts::Delegate,
              public SleepController::Delegate,
              public PageDialogs::Delegate,
              public ExtensionsUi::Delegate,
              public model::BrowserModel::Observer,
              public Favicons::Observer,
              public Updater::Observer,
              public LockTriggers::Delegate {
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

  // Whether the user may act on the browser (the Swift's
  // interactionAllowed): behind the Lock nothing opens, loads, or shows, nor
  // until the data key has opened the profiles and an earlier Pane's have
  // moved in. Every way in asks this one question, as it reaches the
  // intents below: the window's presses and keys, the menus, the switcher,
  // a URL handed over, and the channel, which stands in for them. Only the
  // update's check, download, and dismiss, the window shown again, and Quit
  // go on behind it, as the Swift's menu let them.
  bool InteractionAllowed() const;

  // What the user asks, from the window, the menus, and the channel alike
  // (the Swift's intents). Each that acts on the browser asks
  // InteractionAllowed() first and does nothing without it; a URL handed
  // over waits for it, and Quit, the window shown again, and a Lock (which
  // needs a PIN) go on.

  // The profile sheet's Create: the profile as the form has it, expanded; its
  // id.
  std::string CreateProfile(const model::ProfileDraft& draft);
  // The profile sheet's Save.
  void SaveProfile(const std::string& profile_id,
                   const model::ProfileDraft& draft);
  // Any edit of a profile; a new proxy, fingerprint, or history choice
  // reaches the engine.
  void UpdateProfile(const std::string& profile_id,
                     base::FunctionRef<void(model::BrowserProfile&)> change);
  // The profile sheet, for a new profile or the edited one: its scrim fades
  // in as it slides in from the window's right (the Swift's
  // presentProfileSheet), timed to its first frame.
  void PresentProfileSheet(std::optional<std::string> profile_id);
  // "new", or the edited profile's id, while it shows.
  const std::optional<std::string>& profile_sheet() const {
    return profile_sheet_;
  }
  // A new tab in the profile, selected: at `url`, or at the start page with
  // the address field taking the keys. Its id.
  std::string OpenTab(const std::string& profile_id, std::optional<GURL> url);
  // Selects a tab: its page shows at once, or is made after the frame that
  // answers the input when it has none.
  void Select(const std::string& tab_id) override;
  // A profile collapses, and its pages go; its tabs stay (CONTEXT.md:
  // Unload).
  void CollapseProfile(const std::string& profile_id);
  // The sad tab's Reload, or any reload of a crashed tab: its page loads
  // again, or is made again, its crash loop starting over (ADR 0027).
  void ReloadCrashed(const std::string& tab_id);
  // The browser, or Settings: under Settings no tab is selected, and no
  // profile shows as active (ADR 0064).
  void ShowPage(model::BrowserModel::Page page);
  // A theme reaches the app, its menus, and its pages at once (ADR 0001),
  // and an App icon the Dock (ADR 0089); each is saved as the model saves a
  // change. Settings changes them, and shows only where interaction is
  // allowed (ShowPage); the channel stands in for it at any time, as the
  // Swift's did, so a scenario sees the Lock's panel in either theme.
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
  // ⌘T: a new tab of the profile shown, at the start page, its address
  // field taking the keys.
  void NewTab();
  // ⌘L: the address field, its text selected.
  void FocusAddressField();
  // ⌘1 to ⌘9: the active profile's tab at that place, when it has one (the
  // Swift's selectTab(atIndex:)).
  void SelectTabAt(int number);
  // Opens the switcher on the tab used before (the Swift's nextTabMRU), or
  // moves its selection on; Shift-Tab's goes backward.
  void SwitchTab(bool backward);
  // ⌘Q (ADR 0053): Quit Pane? over the window, or, from it (⌘Q again), at
  // the PIN screen, or with no window to ask in, quitting at once.
  void ConfirmQuit();
  // ⌘F: find in page's bar over the selected tab's page, its field taking
  // the keys (ADR 0067), timed to its first frame.
  void ShowFind();
  void FindNext(bool forward);
  // ⇧⌘I: DevTools for the selected tab's page, docked as they last were, or
  // closed (ADR 0024).
  void ToggleDevTools();
  // The page's menu shown last, until it closes (the channel's).
  PageContextMenus& context_menus() { return *context_menus_; }
  // The profile `profile_id` is, while it is open in the engine.
  Profile* OpenProfile(const std::string& profile_id) const {
    return contexts_.Get(profile_id);
  }
  // Its context, while open: what the engine has of its history choice.
  BrowserContext* OpenContext(const std::string& profile_id) const {
    return contexts_.GetContext(profile_id);
  }
  // Runs `body` with the profile, opening it in the engine first if needed;
  // with null when it would not open (the Swift's contexts.with).
  void WithProfile(const std::string& profile_id,
                   base::OnceCallback<void(Profile*)> body) {
    contexts_.With(profile_id, std::move(body));
  }
  // The menu bar, while the app runs.
  MenuBar& menu_bar() { return *menu_bar_; }

  // The tab switcher, while it shows: its groups, and the tab selected in
  // them, counting down through the groups.
  struct Switcher {
    Switcher();
    Switcher(const Switcher&);
    Switcher& operator=(const Switcher&);
    ~Switcher();

    std::vector<model::SwitcherGroup> groups;
    int selected = 0;
  };
  const std::optional<Switcher>& switcher() const { return switcher_; }

  // ExtensionsUi::Delegate:
  Window& ExtensionsWindow() override;
  bool ExtensionsAllowed() const override;
  Profile* ShownProfile() const override;
  void OpenExtensionTab(Profile& profile, const GURL& url) override;
  // Chrome's extensions page, chrome://extensions, in the profile's tab
  // (D3), timed to the frame that shows the tab and to the page's first
  // paint.
  void ShowExtensionsPage(Profile& profile,
                          const std::string& extension_id) override;
  bool IsPaneProfile(Profile& profile) const override;
  gfx::ImageSkia AppIcon() const override;

  // PageDialogs::Delegate:
  RootView* DialogRoot() override;
  // The selected tab's page, where the user may act.
  content::WebContents* PageForDialogs() override;

  // ProfileContexts::Delegate:
  void ReleasePages(const std::string& profile_id) override;
  bool HasPages(const std::string& profile_id) const override;
  void ShowAgain(const std::string& tab_id) override;

  // SleepController::Delegate:
  // The tab's page, when it has one.
  content::WebContents* PageOf(const std::string& tab_id) const override;
  // The profile in the engine, while it is open.
  Profile* ProfileIfOpen(const std::string& profile_id) const override;
  void SleepTab(const std::string& tab_id) override;
  // Releases every page of a profile; its tabs stay (CONTEXT.md: Unload).
  void UnloadProfile(const std::string& profile_id) override;

  // What shows, for the channel's replies, in the Swift app's shape.
  base::DictValue Snapshot() const;
  // Deleted profiles the engine hasn't confirmed yet.
  size_t pending_profile_deletions() const { return deleting_.size(); }

  // AppData::Client:
  void AccessDidOpen() override;
  void LockRequested() override;
  void SecurityChanged() override;
  void PinShook() override;

  // LockTriggers::Delegate: what locks Pane by itself (L2, ADR 0116).
  void LockFromSystem() override;
  std::optional<base::TimeDelta> LockAfter() const override;
  // A page plays sound or shows in picture-in-picture.
  bool InUse() const override;

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
  AppIconImages& AppIcons() override;
  ExtensionsUi& Extensions() override;
  void EditSettings(
      base::RepeatingCallback<void(model::AppSettings&)> edit) override;
  // The picker over the window, on Settings alone.
  void OpenAppIconPicker() override;
  void ChooseAppIcon(model::AppIcon icon) override;
  void CloseAppIconPicker() override;
  // Settings' Set up, Change, and Remove PIN: the PIN screen, where
  // interaction is allowed.
  void ShowPinScreen(model::PinEntry::Mode mode) override;
  // The PIN screen's keys, typed or pressed, each answered in the next frame:
  // a digit timed to the frame that shows its dot, and the one that completes
  // a PIN to the frame that answers it (a shake, or the Lock sinking).
  void PinDigit(char16_t digit) override;
  void PinBackspace() override;
  void ContinueWithPin() override;
  void DismissPinScreen() override;
  void CheckForUpdates() override;
  void SaveProfileSheet(const model::ProfileDraft& draft) override;
  void CloseProfileSheet() override;
  // The tab the switcher shows selected comes forward, as Ctrl's release
  // chooses it.
  void CommitSwitcher() override;
  void CancelSwitcher() override;
  void CancelQuit() override;
  void QuitNow() override;
  void Find(const std::u16string& text) override;
  void FindStep(bool forward) override;
  void CloseFind() override;
  void ShowContextMenu(content::RenderFrameHost& frame,
                       const content::ContextMenuParams& params) override;

  // CommandRunner, for the menu bar and the window's keys alike: what the
  // Swift's menu enabled (behind the Lock, Quit and Check for Updates…
  // alone; Lock Pane with a PIN), and each command's intent.
  bool IsCommandEnabled(Command command) override;
  void RunCommand(Command command) override;
  std::optional<std::u16string> CommandTitle(Command command) override;
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
  // The profile's most recent tab takes the place of the one that closed.
  void SelectAfterClosing(const std::optional<std::string>& profile_id);
  void ExpandProfile(const std::string& profile_id);
  // The tab goes from the model; when it was selected, its profile's most
  // recent tab takes its place.
  void RemoveTabAndReselect(const std::string& tab_id);
  // The engine answered a delete: the folder goes now, unless a lock came.
  void ProfileDeleted(const std::string& profile_id, bool removes_folder);
  // How far the tab's page has loaded a new document in its main frame, or
  // nullopt while it doesn't (ADR 0081).
  std::optional<double> LoadingOf(const std::string& tab_id) const;
  // Another tab came forward, or the selected one's page went: the bar shows
  // its load as it stands, or hides at once.
  void ShowLoadOf(const std::optional<std::string>& tab_id);
  // What an edit changed reaches the engine: a proxy applied and tested, a
  // fingerprint set and the profile's pages made again, a history choice
  // (the Swift's apply(_:to:)).
  void ApplyProfileChanges(const std::string& profile_id,
                           model::BrowserModel::ProfileChanges changes);
  // A proxy changed: applied to the open profile, and tested for the dot.
  void ProxyDidChange(const std::string& profile_id);
  // A fingerprint changed: the profile's open pages start again in new
  // renderers, which take it; the selected tab loads again.
  void FingerprintDidChange(const std::string& profile_id);

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
  // Chrome put an empty page in the tab's place (an extension's
  // chrome.tabs.discard): the tab shows it, and the selected one loads it.
  void PageReplaced(const std::string& tab_id);
  // The page's first paint with something to see: a woken tab's interval
  // ends with the frame that shows it.
  void PageFirstPaint(const std::string& tab_id);
  // The switcher's tabs, as its view draws them.
  std::vector<TabSwitcherView::Group> SwitcherGroupsToShow();
  // ⇧⌘T: the last tab closed opens again, in its profile, at its address.
  void ReopenClosedTab();
  // ⌘= and ⌘+, ⌘-, ⌘0: the selected page's zoom, traced as the Swift's
  // engine traced it.
  void ZoomPage(Command command);
  // ⌘P: the system's print panel for the selected page (ADR 0021).
  void PrintPage();
  // The model's id of `profile`, one of Pane's open in the engine.
  std::optional<std::string> ProfileIdFor(Profile& profile) const;
  // What the PIN screen shows: the lock's entry, its attempts, the data's
  // move, and at the lock the update on offer.
  LockScreenView::Shows LockShows() const;
  // Chrome's extensions page at `url` in a tab of the profile: the tab that
  // shows the page already, at any of its places (the list, an extension's
  // Details, the shortcuts), going there, else a new one, as Chrome shows it
  // in one tab (ShowSingletonTabOverwritingNTP, IGNORE_AND_NAVIGATE).
  void OpenExtensionsTab(const std::string& profile_id, const GURL& url);

  const raw_ref<AppData> data_;
  // Each profile's proxy, relay, and test, before the contexts, which apply
  // it as each opens.
  ProfileNetwork network_;
  ProfileContexts contexts_;
  // The profile sheet: "new", or the edited profile's id, while it shows.
  std::optional<std::string> profile_sheet_;
  // What the data's opening does once per opening: each proxy tested, each
  // clear still owed run (the Swift's dataKeyDidOpen).
  bool profiles_ready_ = false;
  // The skies of the app's icon, for the Dock and Settings: made before the
  // window, which shows them, and gone after it.
  std::unique_ptr<AppIconImages> app_icons_;
  std::unique_ptr<Window> window_;
  // The page's dialogs, one at a time, the selected tab's (ADR 0021).
  std::unique_ptr<PageDialogs> dialogs_;
  // The page's right-click menu, Chrome's own (U2).
  std::unique_ptr<PageContextMenus> context_menus_;
  // The window's extensions: the toolbar's actions and puzzle, the menus,
  // an action's popup (X1, X2). Made before the window, which shows them,
  // and gone after it.
  std::unique_ptr<ExtensionsUi> extensions_;
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
  // The tab switcher, while it shows.
  std::optional<Switcher> switcher_;
  // A tab selected without a page, timed until its page first paints.
  struct Waking {
    std::string tab_id;
    uint64_t interval = 0;
  };
  std::optional<Waking> waking_;
  // Chrome's extensions page opened, timed until it first paints.
  std::optional<Waking> extensions_page_;
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
  // The Theme the app shows, and the App icon picker over Settings.
  std::optional<model::Theme> applied_theme_;
  bool app_icon_picker_ = false;
  // Pane's own version, and the update row, as Settings shows them.
  const std::u16string version_;
  SettingsUpdate settings_update_;
  // Pane's icon in the Dock.
  std::unique_ptr<DockIcon> dock_icon_;
  // The menu bar, from the command table.
  std::unique_ptr<MenuBar> menu_bar_;
  // Quit Pane? shows.
  bool quit_confirming_ = false;
  base::ScopedObservation<model::BrowserModel, model::BrowserModel::Observer>
      model_observation_{this};
  base::ScopedObservation<Favicons, Favicons::Observer> favicons_observation_{
      this};
  // Sleep, Unload, and Trim, over the open profiles.
  std::unique_ptr<SleepController> sleep_;
  // The update check, download, and install, which the pill reads.
  std::unique_ptr<Updater> updater_;
  // What the pill showed last: whether it was up, and the share downloaded,
  // so an offer and a download's progress are timed to their frames.
  bool pill_shown_ = false;
  std::optional<double> shown_progress_;
  // What locks Pane by itself.
  std::unique_ptr<LockTriggers> lock_triggers_;
  // The intervals of the digit that completed a PIN, until it is answered:
  // as a wrong PIN, or as the Lock opening.
  struct PinAnswer {
    uint64_t wrong = 0;
    uint64_t unlock = 0;
  };
  std::optional<PinAnswer> pin_answer_;
  // Pane was locked when the lock last changed.
  bool was_locked_ = false;
  base::ScopedObservation<Updater, Updater::Observer> updater_observation_{
      this};
  base::WeakPtrFactory<UiApp> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_APP_UI_APP_H_
