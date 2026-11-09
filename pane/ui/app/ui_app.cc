// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/ui_app.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/process/kill.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/referrer.h"
#include "pane/automation/automation.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profiles.h"
#include "pane/model/address_input.h"
#include "pane/model/app_settings.h"
#include "pane/model/model_store.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/model/profile_form.h"
#include "pane/model/tab_history.h"
#include "pane/ui/after_frame.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/app/installed_extensions.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/theme.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/tabs/tab_id.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace pane {

namespace {

UiApp* g_app = nullptr;

// The window's controls, as the Swift app's snapshot gave them: x, y, width,
// height from the window's top left.
base::ListValue ControlFrames(views::Widget* widget) {
  base::ListValue frames;
  if (!widget) {
    return frames;
  }
  for (const gfx::Rect& frame : WindowControlFrames(widget)) {
    frames.Append(base::ListValue()
                      .Append(frame.x())
                      .Append(frame.y())
                      .Append(frame.width())
                      .Append(frame.height()));
  }
  return frames;
}

// The window, as the Swift app's snapshot gave it: its title, whether it
// shows, has the keys, and fills the screen, its frame ("{{x, y}, {w, h}}",
// from the screen's top left), and its controls' frames. A closed window
// shows nothing.
base::DictValue WindowState(views::Widget* widget) {
  const bool open = widget && !widget->IsClosed();
  const gfx::Rect bounds =
      open ? widget->GetWindowBoundsInScreen() : gfx::Rect();
  return base::DictValue()
      .Set("title",
           open && widget->widget_delegate()
               ? base::UTF16ToUTF8(widget->widget_delegate()->GetWindowTitle())
               : std::string())
      .Set("visible", open && widget->IsVisible())
      .Set("key", open && widget->IsActive())
      .Set("fullScreen", open && widget->IsFullscreen())
      .Set("frame",
           base::StringPrintf("{{%d, %d}, {%d, %d}}", bounds.x(), bounds.y(),
                              bounds.width(), bounds.height()))
      .Set("trafficLightFrames", ControlFrames(open ? widget : nullptr));
}

// A profile's swatch, fixed in both appearances (ProfileColor).
SkColor ColorOf(model::ProfileColor color) {
  uint32_t rgb = 0;
  const std::string_view hex = model::HexOf(color);
  if (hex.size() != 7 || !base::HexStringToUInt(hex.substr(1), &rgb)) {
    return SK_ColorGRAY;
  }
  return SkColorSetRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

std::string_view PageName(model::BrowserModel::Page page) {
  switch (page) {
    case model::BrowserModel::Page::kBrowser:
      return "browser";
    case model::BrowserModel::Page::kSettings:
      return "settings";
    case model::BrowserModel::Page::kExtensions:
      return "extensions";
  }
}

std::string_view ProxyStatusName(model::BrowserModel::ProxyStatus status) {
  switch (status) {
    case model::BrowserModel::ProxyStatus::kTesting:
      return "testing";
    case model::BrowserModel::ProxyStatus::kConnected:
      return "connected";
    case model::BrowserModel::ProxyStatus::kFailed:
      return "failed";
  }
}

// How a renderer ended, as the engine's trace has always named it for the
// scenarios (PaneRendererTermination): crashed, killed, out of memory, other.
int TerminationOf(base::TerminationStatus status) {
  switch (status) {
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
      return 0;
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
      return 1;
    case base::TERMINATION_STATUS_OOM:
      return 2;
    default:
      return 3;
  }
}

// The icon draws something at the toolbar's 16 points: a pixel at least 30%
// opaque, as the Swift app's snapshot tells (hasVisiblePixels).
bool HasVisiblePixels(const gfx::Image& icon) {
  constexpr int kSize = 16;
  if (icon.IsEmpty()) {
    return false;
  }
  const gfx::ImageSkia& image = *icon.ToImageSkia();
  if (image.isNull() || image.width() == 0 || image.height() == 0) {
    return false;
  }
  gfx::Canvas canvas(gfx::Size(kSize, kSize), /*image_scale=*/1.0f,
                     /*is_opaque=*/false);
  canvas.DrawImageInt(image, 0, 0, image.width(), image.height(), 0, 0, kSize,
                      kSize, /*filter=*/true);
  const SkBitmap bitmap = canvas.GetBitmap();
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      if (SkColorGetA(bitmap.getColor(x, y)) > 0.3 * 255) {
        return true;
      }
    }
  }
  return false;
}

std::string_view DownloadStateName(
    download::DownloadItem::DownloadState state) {
  switch (state) {
    case download::DownloadItem::COMPLETE:
      return "complete";
    case download::DownloadItem::CANCELLED:
      return "cancelled";
    case download::DownloadItem::INTERRUPTED:
      return "interrupted";
    default:
      return "inProgress";
  }
}

// The profile a page of the window belongs to: its folder's name.
std::optional<std::string> ProfileIdOf(content::WebContents* contents) {
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile) {
    return std::nullopt;
  }
  return profile->GetOriginalProfile()->GetBaseName().AsUTF8Unsafe();
}

}  // namespace

// A tab's page, as the model hears of it: its title and its address, saved
// with the tab (the Swift's didChangeTitle and didChangeURL).
class UiApp::Page : public content::WebContentsObserver {
 public:
  Page(UiApp* app, std::string tab_id, content::WebContents* contents)
      : content::WebContentsObserver(contents),
        app_(app),
        tab_id_(std::move(tab_id)) {}

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override {
    app_->PageTitleChanged(tab_id_);
  }

  // Back's state follows the page's history.
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& details) override {
    app_->PageNavigated(tab_id_);
  }

  void PrimaryPageChanged(content::Page& page) override {
    app_->PageURLChanged(tab_id_);
    app_->PageCameBack(tab_id_);
  }

  // The app hears of it once the process's other observers have, as the
  // engine's TabBridge told the Swift app: a reload from in here would start
  // a renderer mid-notification, before extensions forgot the old process.
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<UiApp> app, std::string tab_id, int termination) {
              PANE_TRACE("callback",
                         "PaneWebContentsDelegate.rendererDidTerminate",
                         base::DictValue().Set("termination", termination));
              if (app) {
                app->PageCrashed(tab_id);
              }
            },
            app_->weak_factory_.GetWeakPtr(), tab_id_, TerminationOf(status)));
  }

  // Destroyed with its page, which was not closed as a tab (the window's or
  // the engine's going): its tab stays.
  void WebContentsDestroyed() override { app_->PageDestroyed(tab_id_); }

 private:
  const raw_ptr<UiApp> app_;
  const std::string tab_id_;
};

// static
void UiApp::Start() {
  CHECK(!g_app);
  g_app = new UiApp();
  // What changed is written as quitting starts, and the profiles let go while
  // the message loop still runs, as Chrome's windows do when they close: they
  // then unload with their processes. The write is waited for before the
  // engine stops, and the volume detaches after (DataDidStop).
  static base::NoDestructor<base::CallbackListSubscription> terminating(
      browser_shutdown::AddAppTerminatingCallback(base::BindOnce([] {
        if (g_app) {
          g_app->SaveNow(base::DoNothing());
        }
        delete std::exchange(g_app, nullptr);
      })));
}

// static
UiApp* UiApp::Get() {
  return g_app;
}

UiApp::UiApp()
    : data_(*AppData::Get()), contexts_(*data_, data_->store().model()) {
  // The design's colors, and the Theme the settings hold, before the window
  // draws: settings.json is read before the lock, as the Swift's was.
  AddPaneColorMixer();
  ApplyTheme(model().settings().theme);
  // A scenario's copy shares the user's preferences, so its window neither
  // reads nor saves the user's frame: 1280 × 800 at every launch (B0).
  window_ = std::make_unique<Window>(
      this,
      /*keeps_frame=*/!base::CommandLine::ForCurrentProcess()->HasSwitch(
          "use-mock-keychain"));
  model_observation_.Observe(&model());
  window_->Open();
  window_->Show(/*activate=*/true);
  events_ = AppEvents::Listen(
      base::BindRepeating(&UiApp::ShowWindow, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&UiApp::OpenHandedURLs, weak_factory_.GetWeakPtr()));

  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  if (std::optional<std::string> directory =
          environment->GetVar("PANE_AUTOMATION_DIR");
      directory && !directory->empty()) {
    automation_ = std::make_unique<Automation>(
        base::FilePath::FromUTF8Unsafe(*directory), this);
  }

  // The lock starts once the window shows: without a PIN the data key opens
  // at once, and the profiles load.
  data_->Start(this);

  std::vector<GURL> urls;
  for (const base::CommandLine::StringType& arg :
       base::CommandLine::ForCurrentProcess()->GetArgs()) {
    const GURL url(base::FilePath(arg).AsUTF8Unsafe());
    if (url.is_valid()) {
      urls.push_back(url);
    }
  }
  OpenLaunchURLs(std::move(urls));
}

UiApp::~UiApp() {
  data_->Stop();
  events_.reset();
  automation_.reset();
  model_observation_.Reset();
  // Their pages go with the window's Browsers, as quitting keeps their tabs.
  pages_.clear();
  window_.reset();
}

model::BrowserModel& UiApp::model() {
  return data_->store().model();
}

const model::BrowserModel& UiApp::model() const {
  return data_->store().model();
}

bool UiApp::InteractionAllowed() const {
  return !data_->IsLocked() && data_->IsOpen();
}

// MARK: The data

// The lock opened, or the data did: once both have, what waited behind them
// goes on.
void UiApp::AccessDidOpen() {
  if (!InteractionAllowed()) {
    return;
  }
  if (!launch_urls_.empty()) {
    OpenLaunchURLs(std::exchange(launch_urls_, {}));
  }
  if (const std::optional<std::string>& active = model().active_tab_id()) {
    if (content::WebContents* page = PageOf(*active)) {
      Show(page);
    }
  }
  if (resume_tab_id_ && model().FindTab(*resume_tab_id_)) {
    Select(*std::exchange(resume_tab_id_, std::nullopt));
  }
}

void UiApp::LockRequested() {
  LockNow();
}

void UiApp::LockNow() {
  if (!data_->HasPin() || data_->IsLocked()) {
    return;
  }
  if (model().active_tab_id()) {
    resume_tab_id_ = model().active_tab_id();
  }
  data_->LockScreen();

  std::vector<std::string> profile_ids;
  for (const model::BrowserProfile& profile : model().profiles()) {
    profile_ids.push_back(profile.id);
  }
  for (const std::string& profile_id : profile_ids) {
    UnloadProfile(profile_id);
  }
  // What changed is written, and the profiles leave memory.
  data_->store().UnloadProfiles();

  // Every profile closes in the engine; the volume detaches once each has.
  // Still open, a profile keeps its files on the volume: Pane quits, which
  // closes every one, and the volume detaches after (ADR 0116).
  contexts_.CloseForLock(base::BindOnce(
      [](base::WeakPtr<UiApp> app, base::OnceCallback<void(bool)> close_data,
         bool every_closed) {
        std::move(close_data).Run(every_closed);
        if (!every_closed) {
          LOG(ERROR) << "A profile was still open after Pane locked: Pane "
                        "quits to close it";
          if (app) {
            app->Quit(base::DoNothing());
          }
        }
      },
      weak_factory_.GetWeakPtr(), data_->CloseData()));
}

// MARK: Intents

std::string UiApp::CreateProfile(const model::ProfileDraft& draft) {
  const std::string id =
      model().CreateProfile([&](model::BrowserProfile& profile) {
        draft.ApplyTo(profile, [&] {
          return model::GeneratedFingerprint(draft.platform, HostWebGL());
        });
      });
  ExpandProfile(id);
  return id;
}

std::string UiApp::OpenTab(const std::string& profile_id,
                           std::optional<GURL> url) {
  ExpandProfile(profile_id);
  const std::string tab_id = NewTabId();
  model().InsertTab(
      {.id = tab_id,
       .url = url ? url->possibly_invalid_spec() : model::kNewTabURL},
      profile_id);
  if (!url) {
    focuses_address_ = tab_id;
  }
  Select(tab_id);
  return tab_id;
}

void UiApp::Select(const std::string& tab_id) {
  const model::Tab* tab = model().FindTab(tab_id);
  const std::optional<std::string> profile_id = model().ProfileIdOfTab(tab_id);
  if (!InteractionAllowed() || !tab || !profile_id) {
    return;
  }
  const std::u16string address = base::UTF8ToUTF16(tab->url);
  // ⌘T's tab is timed as a new tab (NewTab).
  const bool new_tab = focuses_address_ == tab_id;
  if (model().active_tab_id() != tab_id && !new_tab) {
    LogInteraction(Interaction::kSelectTab, window_->compositor());
  }
  model().SelectTab(tab_id);
  model().SetPage(model::BrowserModel::Page::kBrowser);
  model().SetExpanded(*profile_id, true);

  // The window's one WebView swaps the page left for the one entered in the
  // frame that answers the input.
  if (content::WebContents* page = PageOf(tab_id)) {
    Show(page);
    return;
  }

  // The engine makes the page after the frame that answers the input (11 ms
  // for a new tab's): the row selected, and a new tab's address field, don't
  // wait for it.
  window_->ShowNoPage(address);
  if (RootView* root = window_->root(); root && new_tab) {
    // A key typed right behind ⌘T goes in the field, not the page left.
    root->FocusAddress();
  }
  AfterFrame(
      window_->compositor(),
      base::BindOnce(
          [](base::WeakPtr<UiApp> app, std::string tab_id,
             std::string profile_id) {
            if (!app) {
              return;
            }
            app->contexts_.With(
                profile_id,
                base::BindOnce(
                    [](base::WeakPtr<UiApp> app, std::string tab_id,
                       Profile* profile) {
                      if (!app || app->model().active_tab_id() != tab_id ||
                          app->PageOf(tab_id) || !app->InteractionAllowed()) {
                        return;
                      }
                      // The profile would not open: the sad tab's place,
                      // which Phase 3 draws; selecting it tries again.
                      if (!profile) {
                        app->crashed_tab_ids_.insert(tab_id);
                        return;
                      }
                      app->OpenPage(tab_id, profile, /*foreground=*/true);
                    },
                    app, tab_id));
          },
          weak_factory_.GetWeakPtr(), tab_id, *profile_id));
}

void UiApp::ChangeSettings(
    base::FunctionRef<void(model::AppSettings&)> change) {
  model().ChangeSettings(change);
}

void UiApp::SaveNow(base::OnceClosure written) {
  data_->store().SaveNow(std::move(written));
}

void UiApp::Quit(base::OnceClosure saved) {
  // Saved, then closed: the window, its profiles, then the engine, after
  // which the volume detaches.
  SaveNow(std::move(saved).Then(base::BindOnce(&chrome::AttemptUserExit)));
}

void UiApp::OpenLaunchURLs(std::vector<GURL> urls) {
  if (!InteractionAllowed()) {
    launch_urls_.insert(launch_urls_.end(), urls.begin(), urls.end());
    return;
  }
  if (urls.empty()) {
    return;
  }
  std::string profile_id;
  if (model().active_profile_id()) {
    profile_id = *model().active_profile_id();
  } else if (!model().profiles().empty()) {
    profile_id = model().profiles().front().id;
  } else {
    profile_id = model().CreateProfile("Personal", model::ProfileColor::kBlue,
                                       std::nullopt);
  }
  ExpandProfile(profile_id);
  // Each loads at once, as Chrome loads every URL it is handed; then the last
  // shows.
  std::string last;
  for (const GURL& url : urls) {
    last = OpenTabBehind(profile_id, url, std::nullopt);
  }
  Select(last);
}

std::string UiApp::OpenTabBehind(const std::string& profile_id,
                                 const GURL& url,
                                 const std::optional<std::string>& anchor) {
  const std::string tab_id = NewTabId();
  model().InsertTab({.id = tab_id, .url = url.possibly_invalid_spec()},
                    profile_id, anchor);
  contexts_.With(
      profile_id,
      base::BindOnce(
          [](base::WeakPtr<UiApp> app, std::string tab_id, Profile* profile) {
            if (!app || !profile || !app->model().FindTab(tab_id) ||
                app->PageOf(tab_id)) {
              return;
            }
            app->OpenPage(tab_id, profile, /*foreground=*/false);
          },
          weak_factory_.GetWeakPtr(), tab_id));
  return tab_id;
}

// MARK: Pages

void UiApp::OpenPage(const std::string& tab_id,
                     Profile* profile,
                     bool foreground) {
  const model::Tab* tab = model().FindTab(tab_id);
  Browser* browser = window_->BrowserFor(profile);
  if (!tab || !browser) {
    return;
  }
  GURL url(tab->url);
  if (!url.is_valid()) {
    url = GURL(model::kNewTabURL);
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = foreground ? WindowOpenDisposition::kNewForegroundTab
                                  : WindowOpenDisposition::kNewBackgroundTab;
  params.window_action = NavigateParams::WindowAction::kNoAction;
  {
    base::AutoReset<WindowHost::PendingOpen> pending(
        &window_->host()->pending_open(),
        WindowHost::PendingOpen{params.disposition, nullptr});
    base::AutoReset<std::optional<std::string>> opening(&opening_tab_id_,
                                                        tab_id);
    Navigate(&params);
  }
  content::WebContents* contents = params.navigated_or_inserted_contents;
  if (contents && !PageOf(tab_id)) {
    AddPage(tab_id, contents);
  }
  if (contents && model().active_tab_id() == tab_id) {
    Show(contents);
  }
}

void UiApp::AdoptOpeningTab(content::WebContents* contents) {
  if (opening_tab_id_ && !TabWithId(*opening_tab_id_)) {
    SetTabId(contents, *opening_tab_id_);
  }
}

void UiApp::AddPage(const std::string& tab_id, content::WebContents* contents) {
  pages_[tab_id] = std::make_unique<Page>(this, tab_id, contents);
  // A page of its own again, its profile open.
  crashed_tab_ids_.erase(tab_id);
}

content::WebContents* UiApp::PageOf(const std::string& tab_id) const {
  const auto page = pages_.find(tab_id);
  return page == pages_.end() ? nullptr : page->second->web_contents();
}

Profile* UiApp::ProfileIfOpen(const std::string& profile_id) const {
  return contexts_.Get(profile_id);
}

// Never behind the lock: no page sits under the PIN screen.
void UiApp::Show(content::WebContents* contents) {
  if (!InteractionAllowed()) {
    return;
  }
  const std::string& tab_id = TabIdOf(contents);
  // A new tab's address field takes the keys once it shows, to type in;
  // what has been typed since ⌘T stays.
  const bool new_tab = focuses_address_ == tab_id;
  if (new_tab) {
    focuses_address_.reset();
  }
  window_->ShowTab(contents, /*chosen=*/!new_tab);
}

void UiApp::Release(const std::string& tab_id) {
  const auto found = pages_.find(tab_id);
  if (found == pages_.end()) {
    return;
  }
  content::WebContents* contents = found->second->web_contents();
  // Unmapped first: its removal is no tab closing.
  pages_.erase(found);
  // A released tab's next page starts over.
  crashes_.Forget(tab_id);
  crashed_tab_ids_.erase(tab_id);
  if (window_->selected() == contents) {
    window_->ClearPage();
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  const int index =
      strip ? strip->GetIndexOfWebContents(contents) : TabStripModel::kNoTab;
  if (index != TabStripModel::kNoTab) {
    strip->DetachAndDeleteWebContentsAt(index);
  }
}

// Releases every page of a profile; its tabs stay (CONTEXT.md: Unload).
void UiApp::UnloadProfile(const std::string& profile_id) {
  const model::BrowserProfile* profile = model().FindProfile(profile_id);
  if (!profile) {
    return;
  }
  std::vector<std::string> tab_ids;
  for (const model::Tab& tab : profile->tabs) {
    tab_ids.push_back(tab.id);
  }
  for (const std::string& tab_id : tab_ids) {
    Release(tab_id);
  }
  if (model().active_profile_id() == profile_id) {
    window_->ClearPage();
    model().SelectTab(std::nullopt);
    model().SetActiveProfile(std::nullopt);
  }
}

void UiApp::SelectAfterClosing(const std::optional<std::string>& profile_id) {
  const model::BrowserProfile* profile =
      profile_id ? model().FindProfile(*profile_id) : nullptr;
  std::vector<std::string> tab_ids;
  if (profile) {
    for (const model::Tab& tab : profile->tabs) {
      tab_ids.push_back(tab.id);
    }
  }
  if (const std::optional<std::string> next =
          model::MostRecentTab(tab_ids, model().history().mru())) {
    Select(*next);
    return;
  }
  window_->ShowNoPage(std::u16string());
  model().SelectTab(std::nullopt);
  model().SetActiveProfile(profile_id);
}

void UiApp::ExpandProfile(const std::string& profile_id) {
  model().SetExpanded(profile_id, true);
}

// The sidebar header's Collapse all: every expanded profile collapses, its
// pages unloading (the Swift's collapseAll).
void UiApp::CollapseAll() {
  const std::set<std::string> expanded = model().expanded_profile_ids();
  for (const std::string& profile_id : expanded) {
    model().SetExpanded(profile_id, false);
    UnloadProfile(profile_id);
  }
}

// The toolbar's Back: the shown page's history, a step back.
void UiApp::Back() {
  content::WebContents* page = window_->selected();
  if (page && page->GetController().CanGoBack()) {
    page->GetController().GoBack();
  }
}

// The window shows again, in front (the Dock's reopen, an app handing over
// URLs): a new one if it was closed, which shows what the app has, the
// selected tab's page made again after the frame that answers.
void UiApp::ShowWindow() {
  const bool opened = !window_->is_open();
  if (opened) {
    window_->Open();
  }
  window_->Show(/*activate=*/true);
  if (const std::optional<std::string> active = model().active_tab_id();
      opened && active &&
      model().page() == model::BrowserModel::Page::kBrowser) {
    Select(*active);
  }
}

void UiApp::OpenHandedURLs(std::vector<GURL> urls) {
  ShowWindow();
  OpenLaunchURLs(std::move(urls));
}

// A click on a profile's header: it opens, or collapsing unloads it (its
// pages go, its tabs stay).
void UiApp::ToggleProfile(const std::string& profile_id) {
  if (model().expanded_profile_ids().contains(profile_id)) {
    model().SetExpanded(profile_id, false);
    UnloadProfile(profile_id);
  } else {
    ExpandProfile(profile_id);
  }
}

void UiApp::PageTitleChanged(const std::string& tab_id) {
  content::WebContents* contents = PageOf(tab_id);
  if (!contents) {
    return;
  }
  const std::string title = base::UTF16ToUTF8(contents->GetTitle());
  model().UpdateTab(tab_id, [&](model::Tab& tab) { tab.title = title; });
  if (window_->selected() == contents) {
    window_->UpdateTitle();
  }
}

void UiApp::PageNavigated(const std::string& tab_id) {
  if (content::WebContents* contents = PageOf(tab_id);
      contents && window_->selected() == contents) {
    window_->Refresh();
  }
}

void UiApp::PageURLChanged(const std::string& tab_id) {
  content::WebContents* contents = PageOf(tab_id);
  if (!contents) {
    return;
  }
  const std::string url = contents->GetVisibleURL().spec();
  model().UpdateTab(tab_id, [&](model::Tab& tab) { tab.url = url; });
}

void UiApp::PageDestroyed(const std::string& tab_id) {
  pages_.erase(tab_id);
}

// The first crash reloads the page, as a slept tab wakes. A page that dies
// again within a minute stays crashed instead of looping, as does one behind
// another tab, until it reloads (the Swift's rendererDidTerminate). Only a
// renderer Chrome would show its sad tab for counts: not one that ended
// normally, nor a page reloaded meanwhile.
void UiApp::PageCrashed(const std::string& tab_id) {
  content::WebContents* page = PageOf(tab_id);
  if (!page || !page->IsCrashed()) {
    return;
  }
  if (crashes_.Crashed(tab_id, base::Time::Now()) ||
      model().active_tab_id() != tab_id) {
    crashed_tab_ids_.insert(tab_id);
    return;
  }
  page->GetController().Reload(content::ReloadType::NORMAL,
                               /*check_for_repost=*/true);
}

// A crashed tab reloaded starts over, as the Swift's reloadCrashed: its next
// crash is a first one. The automatic reload of a first crash, never marked,
// keeps the loop's memory.
void UiApp::PageCameBack(const std::string& tab_id) {
  if (crashed_tab_ids_.erase(tab_id)) {
    crashes_.Forget(tab_id);
  }
}

// MARK: Window::Controller

std::vector<SidebarRow> UiApp::Rows() {
  std::vector<SidebarRow> rows;
  const model::BrowserModel& model = this->model();
  for (const model::BrowserProfile& profile : model.profiles()) {
    rows.push_back({.profile_id = profile.id,
                    .title = base::UTF8ToUTF16(profile.name),
                    .selected = model.active_profile_id() == profile.id,
                    .color = ColorOf(profile.color)});
    if (!model.expanded_profile_ids().contains(profile.id)) {
      continue;
    }
    for (const model::Tab& tab : profile.tabs) {
      rows.push_back(
          {.profile_id = profile.id,
           .tab_id = tab.id,
           .title = base::UTF8ToUTF16(tab.title.empty() ? tab.url : tab.title),
           .selected = model.active_tab_id() == tab.id});
    }
  }
  return rows;
}

RootView::Shows UiApp::Shows() {
  const model::BrowserModel& model = this->model();
  content::WebContents* page = window_->selected();
  return {
      .content = model.page() != model::BrowserModel::Page::kBrowser
                     ? RootView::Content::kOther
                 : model.active_tab_id() ? RootView::Content::kPage
                                         : RootView::Content::kEmpty,
      .sidebar_width = model.settings().sidebar_width,
      .can_go_back = page && page->GetController().CanGoBack(),
      .can_collapse_all = !model.expanded_profile_ids().empty(),
  };
}

// The window's pages went with it (PageDestroyed); their tabs stay, and the
// selected one's page is made again when the window shows.
void UiApp::WindowClosed() {
  focuses_address_.reset();
}

void UiApp::RowPressed(const SidebarRow& row) {
  if (row.tab_id.empty()) {
    ToggleProfile(row.profile_id);
    return;
  }
  Select(row.tab_id);
}

// What the address field loads (the Swift's navigate): the selected page goes
// there, or a tab with none yet is made there, or a new tab opens there.
void UiApp::AddressEntered(const std::u16string& text) {
  const GURL url = model::AddressURL(base::UTF16ToUTF8(text));
  if (!url.is_valid() || !InteractionAllowed()) {
    return;
  }
  const std::optional<std::string>& active = model().active_tab_id();
  if (content::WebContents* page = active ? PageOf(*active) : nullptr) {
    page->GetController().LoadURL(url, content::Referrer(),
                                  ui::PAGE_TRANSITION_TYPED, std::string());
    page->Focus();
    return;
  }
  if (active) {
    const std::string tab_id = *active;
    // Entered before ⌘T's page came: it is made at the address entered, and
    // shows as a tab chosen.
    if (focuses_address_ == tab_id) {
      focuses_address_.reset();
    }
    model().UpdateTab(tab_id, [&](model::Tab& tab) { tab.url = url.spec(); });
    Select(tab_id);
    return;
  }
  if (const std::optional<std::string>& profile_id =
          model().active_profile_id()) {
    OpenTab(*profile_id, url);
  } else if (!model().profiles().empty()) {
    OpenTab(model().profiles().front().id, url);
  }
}

// ⌘T: a new tab of the profile shown, at the start page.
void UiApp::NewTab() {
  LogInteraction(Interaction::kNewTab, window_->compositor());
  std::optional<std::string> profile_id = model().active_profile_id();
  if (!profile_id && !model().profiles().empty()) {
    profile_id = model().profiles().front().id;
  }
  if (!profile_id || !InteractionAllowed()) {
    return;
  }
  OpenTab(*profile_id, std::nullopt);
}

void UiApp::SelectTabAt(int number) {
  const std::optional<std::string>& profile_id = model().active_profile_id();
  const model::BrowserProfile* profile =
      profile_id ? model().FindProfile(*profile_id) : nullptr;
  if (!profile || profile->tabs.empty()) {
    return;
  }
  const int count = static_cast<int>(profile->tabs.size());
  const int index = number == 9 ? count - 1 : std::min(number, count) - 1;
  Select(profile->tabs[index].id);
}

void UiApp::TabInserted(content::WebContents* contents,
                        content::WebContents* opener,
                        bool foreground) {
  // The page the app had the engine make for one of its tabs.
  if (opening_tab_id_) {
    AdoptOpeningTab(contents);
    if (!PageOf(*opening_tab_id_)) {
      AddPage(*opening_tab_id_, contents);
    }
    return;
  }

  // A page opened it (a popup, a link in a new tab): a new tab after its
  // opener, in the opener's profile.
  const std::optional<std::string> profile_id = ProfileIdOf(contents);
  if (!profile_id || !model().FindProfile(*profile_id)) {
    return;
  }
  const std::string tab_id = TabIdOf(contents);
  std::optional<std::string> after;
  if (opener) {
    after = TabIdOf(opener);
  }
  model().InsertTab({.id = tab_id, .url = contents->GetVisibleURL().spec()},
                    *profile_id, after);
  AddPage(tab_id, contents);
  // Chromium is mid-call on its tab strip: selected on the next turn.
  if (foreground) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&UiApp::Select, weak_factory_.GetWeakPtr(), tab_id));
  }
}

void UiApp::TabRemoved(content::WebContents* contents) {
  // A page the app released is no longer its tab's: the tab stays.
  const auto found = std::ranges::find_if(pages_, [&](const auto& page) {
    return page.second->web_contents() == contents;
  });
  if (found == pages_.end()) {
    return;
  }
  // The page closed: the app closed it (once beforeunload agreed), or it
  // closed itself (window.close()). Either way its tab goes too, and when it
  // was selected, the next one is chosen once Chromium has left its tab strip.
  const std::string tab_id = found->first;
  pages_.erase(found);
  crashes_.Forget(tab_id);
  const bool was_active = model().active_tab_id() == tab_id;
  const std::optional<std::string> profile_id = model().ProfileIdOfTab(tab_id);
  model().RemoveTab(tab_id);
  if (was_active) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&UiApp::SelectAfterClosing,
                                  weak_factory_.GetWeakPtr(), profile_id));
  }
}

// Chrome made another tab active: an extension's tabs.update, say. The app
// selects it too, on the next turn, unless the selection moved meanwhile.
void UiApp::ActiveTabChanged(content::WebContents* contents) {
  // Chrome tells its Browser, which tells this, before the insert.
  AdoptOpeningTab(contents);
  if (opening_tab_id_) {
    return;
  }
  const auto found = std::ranges::find_if(pages_, [&](const auto& page) {
    return page.second->web_contents() == contents;
  });
  if (found == pages_.end() || model().active_tab_id() == found->first) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<UiApp> app, std::string tab_id,
             std::optional<std::string> previous) {
            if (!app || app->model().active_tab_id() != previous ||
                (previous && !app->model().FindTab(*previous)) ||
                !app->PageOf(tab_id)) {
              return;
            }
            app->Select(tab_id);
          },
          weak_factory_.GetWeakPtr(), found->first, model().active_tab_id()));
}

// MARK: model::BrowserModel::Observer

void UiApp::OnProfilesChanged() {
  // A tab that left the model, closed or behind the lock, is crashed no more.
  std::erase_if(crashed_tab_ids_, [&](const std::string& tab_id) {
    return !model().FindTab(tab_id);
  });
  window_->Refresh();
}

void UiApp::OnSettingsChanged() {
  // A Theme chosen reaches the app, its menus, and its pages at once (ADR
  // 0001); the rest of the settings wait for Settings' port.
  ApplyTheme(model().settings().theme);
}

void UiApp::OnExpandedProfilesChanged() {
  window_->Refresh();
}

void UiApp::OnActiveTabChanged() {
  window_->Refresh();
}

void UiApp::OnActiveProfileChanged() {
  window_->Refresh();
}

// MARK: The channel

base::DictValue UiApp::Snapshot() const {
  const model::BrowserModel& model = this->model();
  base::ListValue profiles;
  for (const model::BrowserProfile& profile : model.profiles()) {
    base::ListValue tabs;
    for (const model::Tab& tab : profile.tabs) {
      content::WebContents* page = PageOf(tab.id);
      tabs.Append(
          base::DictValue()
              .Set("id", tab.id)
              .Set("url", tab.url)
              .Set("title", tab.title)
              .Set("hasFavicon", !tab.favicon.empty())
              .Set("loaded", page != nullptr)
              .Set("loading", page && page->ShouldShowLoadingUI())
              .Set("canGoBack", page && page->GetController().CanGoBack())
              .Set("canGoForward",
                   page && page->GetController().CanGoForward()));
    }
    const auto status = model.proxy_status().find(profile.id);
    profiles.Append(
        base::DictValue()
            .Set("id", profile.id)
            .Set("name", profile.name)
            .Set("color", model::ColorName(profile.color))
            .Set("expanded", model.expanded_profile_ids().contains(profile.id))
            .Set("proxyStatus",
                 status == model.proxy_status().end()
                     ? base::Value()
                     : base::Value(ProxyStatusName(status->second)))
            .Set("keepLoaded", profile.keep_loaded)
            .Set("savesHistory", profile.saves_history)
            .Set("tabs", std::move(tabs)));
  }
  // Each open profile's extensions, their actions as for the profile's page
  // the window shows (the Swift's ExtensionsController.lists), and its
  // downloads, as the profile's download manager holds them.
  base::DictValue extensions;
  base::ListValue downloads;
  content::WebContents* shown = window_->selected();
  for (const model::BrowserProfile& profile : model.profiles()) {
    Profile* open = contexts_.Get(profile.id);
    if (!open) {
      continue;
    }
    content::WebContents* tab =
        shown && Profile::FromBrowserContext(shown->GetBrowserContext()) == open
            ? shown
            : nullptr;
    base::ListValue installed;
    for (const InstalledExtension& extension : InstalledExtensions(open, tab)) {
      installed.Append(
          base::DictValue()
              .Set("id", extension.id)
              .Set("name", extension.name)
              .Set("version", extension.version)
              .Set("enabled", extension.enabled)
              .Set("pinned", extension.pinned)
              .Set("actionTitle", extension.action_title)
              .Set("iconVisible", HasVisiblePixels(extension.icon)));
    }
    extensions.Set(profile.id, std::move(installed));

    download::SimpleDownloadManager::DownloadVector items;
    open->GetDownloadManager()->GetAllDownloads(&items);
    for (const download::DownloadItem* item : items) {
      const base::FilePath& path = item->GetTargetFilePath();
      downloads.Append(base::DictValue()
                           .Set("id", item->GetGuid())
                           .Set("url", item->GetURL().spec())
                           .Set("path", path.empty()
                                            ? base::Value()
                                            : base::Value(path.AsUTF8Unsafe()))
                           .Set("receivedBytes",
                                static_cast<double>(item->GetReceivedBytes()))
                           .Set("state", DownloadStateName(item->GetState())));
    }
  }
  base::ListValue crashed;
  for (const std::string& tab_id : crashed_tab_ids_) {
    crashed.Append(tab_id);
  }

  views::Widget* widget = window_->widget();
  RootView* root = window_->root();
  const std::optional<std::string>& active_tab = model.active_tab_id();
  const std::optional<std::string>& active_profile = model.active_profile_id();
  return base::DictValue()
      .Set("profiles", std::move(profiles))
      .Set("activeTabID", active_tab ? base::Value(*active_tab) : base::Value())
      .Set("activeProfileID",
           active_profile ? base::Value(*active_profile) : base::Value())
      .Set("page", PageName(model.page()))
      .Set("theme", model::ThemeName(model.settings().theme))
      .Set("appIcon", model::AppIconName(model.settings().app_icon))
      .Set("sidebarWidth", model.settings().sidebar_width)
      .Set("closedTabs", static_cast<int>(model.history().closed().size()))
      .Set("extensions", std::move(extensions))
      .Set("downloads", std::move(downloads))
      .Set("crashedTabs", std::move(crashed))
      .Set("address", root ? root->address() : std::u16string())
      .Set("focus", root ? root->focus() : std::string("none"))
      .Set("pageFullscreen", window_->page_fullscreen())
      .Set("window", WindowState(widget))
      .Set("security", data_->State());
}

}  // namespace pane
