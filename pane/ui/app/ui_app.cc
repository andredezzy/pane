// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/ui_app.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/process/kill.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/printing/print_view_manager_common.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/download/public/common/download_item.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver_observer.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/zoom/page_zoom.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/page_zoom.h"
#include "content/public/common/referrer.h"
#include "pane/app/version.h"
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
#include "pane/model/test_hooks.h"
#include "pane/ui/after_frame.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/app/dock_icon.h"
#include "pane/ui/app/installed_extensions.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/theme.h"
#include "pane/ui/dialogs/permission_prompt.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/page/devtools.h"
#include "pane/ui/page/find_in_page.h"
#include "pane/ui/panels/app_icon_images.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/panels/time_of_day.h"
#include "pane/ui/sidebar/sidebar_content.h"
#include "pane/ui/tabs/tab_id.h"
#include "pane/ui/toolbar/address_field.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace pane {

namespace {

UiApp* g_app = nullptr;

// The start page a new tab opens at (the Swift's AddressInput.newTabURL):
// Google's, or in a scenario's run the page it names (PANE_NEW_TAB_URL), so
// its new tabs load from its own server.
const std::string& NewTabURL() {
  static const base::NoDestructor<std::string> url(
      model::TestHook("PANE_NEW_TAB_URL").value_or(model::kNewTabURL));
  return *url;
}

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

// The toolbar as it shows (the Swift's read it through accessibility): its
// controls, whether each takes presses and what it reads as, and whether the
// address shows the page's domain and title over its text.
base::DictValue ToolbarState(ToolbarView& toolbar) {
  base::DictValue controls;
  for (const auto& [name, view] : toolbar.NamedViews()) {
    controls.Set(
        name,
        base::DictValue()
            .Set("enabled", view->GetEnabled())
            .Set("visible", view->IsDrawn())
            .Set("name", base::UTF16ToUTF8(
                             view->GetViewAccessibility().GetCachedName())));
  }
  return base::DictValue()
      .Set("controls", std::move(controls))
      .Set("addressLabel", toolbar.address_field()->label_shown());
}

// The update as the pill, Settings, and the Lock show it, in the Swift app's
// snapshot's shape.
base::DictValue UpdateState(const Updater& updater) {
  const std::optional<model::AvailableUpdate>& available = updater.available();
  const std::optional<double> progress = updater.download_progress();
  return base::DictValue()
      .Set("status", UpdateStatusName(updater.status()))
      .Set("available",
           available ? base::Value(available->version) : base::Value())
      .Set("progress", progress ? base::Value(*progress) : base::Value())
      .Set("pill", updater.ShowsPill())
      .Set("label", base::UTF16ToUTF8(updater.Label()))
      // Staged beside the app, to install at quit (ADR 0092).
      .Set("installsItself", updater.staged())
      .Set("checked", updater.checked());
}

// The update row as Settings shows it (the Swift's UpdateRow): what the last
// check found, or the version on offer, and the row's button.
SettingsUpdate SettingsUpdateOf(const Updater& updater) {
  SettingsUpdate update;
  if (const std::optional<model::AvailableUpdate>& available =
          updater.available()) {
    const std::u16string version = base::UTF8ToUTF16(available->version);
    update.downloads = true;
    switch (updater.status()) {
      case Updater::Status::kDownloaded:
        // Staged, it restarts into the update (ADR 0092); otherwise it shows
        // the installer and quits, for the user to drag Pane in (Paper
        // 06.3.1).
        update.status =
            updater.staged()
                ? base::StrCat({u"Version ", version,
                                u" is ready. Pane restarts into it."})
                : base::StrCat({u"Version ", version,
                                u" is downloaded. It installs by hand."});
        update.button = updater.Label();
        break;
      case Updater::Status::kDownloading: {
        update.status = base::StrCat({u"Version ", version, u" is available."});
        const int percent = static_cast<int>(
            std::lround(updater.download_progress().value_or(0) * 100));
        update.button = base::StrCat(
            {u"Downloading\u2026 ", base::NumberToString16(percent), u"%",
             updater.download_eta()
                 ? base::StrCat(
                       {u" \u00B7 ", base::UTF8ToUTF16(model::FormatETA(
                                         *updater.download_eta()))})
                 : std::u16string()});
        update.button_enabled = false;
        break;
      }
      default:
        update.status = base::StrCat({u"Version ", version, u" is available."});
        update.button = u"Download update";
        break;
    }
    return update;
  }
  if (const std::optional<base::Time> checked = updater.last_checked_at()) {
    update.status = base::StrCat(
        {u"Pane is up to date \u00B7 checked at ", TimeOfDay(*checked)});
    update.up_to_date = true;
  } else {
    update.status = u"Pane checks for a new version on its own.";
  }
  const bool checking = updater.status() == Updater::Status::kChecking;
  update.button = checking ? u"Checking\u2026" : u"Check for updates";
  update.button_enabled = !checking;
  return update;
}

// Find in page's bar as it shows, in the Swift app's snapshot's shape.
base::DictValue FindState(RootView* root) {
  const FindBarView::State state =
      root ? root->find_state() : FindBarView::State();
  return base::DictValue()
      .Set("visible", state.visible)
      .Set("text", state.text)
      .Set("matches", state.matches.value_or(0))
      .Set("activeMatch", state.active);
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
class UiApp::Page : public content::WebContentsObserver,
                    public favicon::FaviconDriverObserver {
 public:
  Page(UiApp* app, std::string tab_id, content::WebContents* contents)
      : content::WebContentsObserver(contents),
        app_(app),
        tab_id_(std::move(tab_id)) {
    // Its permission requests ask in the window's own dialog.
    PermissionPrompt::Install(contents);
    // Its favicon, from Chrome's driver, as it comes.
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(contents)) {
      favicon_observation_.Observe(driver);
    }
  }

  // The favicon the driver has now, which a page opened by another may have
  // before this heard of it.
  gfx::Image Favicon() const {
    auto* driver =
        favicon::ContentFaviconDriver::FromWebContents(web_contents());
    return driver && driver->FaviconIsValid() ? driver->GetFavicon()
                                              : gfx::Image();
  }

  // favicon::FaviconDriverObserver:
  void OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                        NotificationIconType notification_icon_type,
                        const GURL& icon_url,
                        bool icon_url_changed,
                        const gfx::Image& image) override {
    if (notification_icon_type == NON_TOUCH_16_DIP && !image.IsEmpty()) {
      app_->PageFaviconChanged(tab_id_, image);
    }
  }

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override {
    app_->PageTitleChanged(tab_id_);
  }

  // How far the page has loaded a new document in its main frame, nullopt
  // while it doesn't.
  std::optional<double> loading() const { return loading_; }

  // The page is loading only while a new document loads in its main frame,
  // as Chrome's own tab and toolbar show it (ShouldShowLoadingUI, ADR 0081).
  // A frame tree that loads only iframes starts and stops loading too, under
  // a page already shown. These three cover every change between the two: a
  // main frame's load reports its progress as it starts and as it ends,
  // each after its state changed, and the frame tree's start and stop cover
  // the rest.
  void DidStartLoading() override { ReportLoading(0); }
  void DidStopLoading() override { ReportLoading(1); }
  void LoadProgressChanged(double progress) override {
    ReportLoading(progress);
  }

  // Back's and Forward's states follow the page's history.
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& details) override {
    app_->PageNavigated(tab_id_);
  }

  void NavigationListPruned(
      const content::PrunedDetails& pruned_details) override {
    app_->PageNavigated(tab_id_);
  }

  void NavigationEntriesDeleted() override { app_->PageNavigated(tab_id_); }

  void PrimaryPageChanged(content::Page& page) override {
    app_->PageURLChanged(tab_id_);
    app_->PageCameBack(tab_id_);
  }

  // The app hears of it once the process's other observers have, as the
  // engine's TabBridge told the Swift app: a reload from in here would start
  // a renderer mid-notification, before extensions forgot the old process.
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    // A dead page loads nothing.
    loading_.reset();
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

  // Chrome discards a tab (an extension's chrome.tabs.discard) by putting an
  // empty page in its place, which keeps its history, and destroying this
  // one: the tab follows it from now on, as its id does, and loads it when
  // shown (ADR 0041). Reporting the discard as a closing was rejected: the
  // tab would leave the sidebar.
  void AboutToBeDiscarded(content::WebContents* new_contents) override {
    favicon_observation_.Reset();
    SetTabId(new_contents, tab_id_);
    Observe(new_contents);
    loading_.reset();
    discarded_ = true;
  }

  // The empty page is in the tab strip, with its tab helpers.
  void WasDiscarded() override {
    PermissionPrompt::Install(web_contents());
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      favicon_observation_.Observe(driver);
    }
    app_->PageReplaced(tab_id_);
  }

  // A discarded page loads once, when the app shows it: Chrome loads one
  // only as its focused window's active tab, which a Pane window behind the
  // app in use never is. Once, since Chrome's mark of a discard stays for the
  // page's life (FrameTreeNode::was_discarded_), where the Swift's bridge
  // read it and loaded the page again at every show.
  void LoadIfDiscarded() {
    if (!std::exchange(discarded_, false)) {
      return;
    }
    PANE_TRACE(
        "call", "PaneWebContents.reloadDiscarded",
        base::DictValue().Set(
            "tab", sessions::SessionTabHelper::IdForTab(web_contents()).id()));
    content::NavigationController& controller = web_contents()->GetController();
    controller.SetNeedsReload();
    controller.LoadIfNecessary();
  }

  // Its first paint with something to see (a woken tab's timing).
  void DidFirstVisuallyNonEmptyPaint() override {
    app_->PageFirstPaint(tab_id_);
  }

 private:
  void ReportLoading(double progress) {
    const std::optional<double> from = loading_;
    loading_ = web_contents()->ShouldShowLoadingUI()
                   ? std::optional<double>(progress)
                   : std::nullopt;
    app_->PageLoadingChanged(tab_id_, from, loading_);
  }

  const raw_ptr<UiApp> app_;
  const std::string tab_id_;
  std::optional<double> loading_;
  // Chrome's empty page, in a discarded one's place, not yet loaded.
  bool discarded_ = false;
  base::ScopedObservation<favicon::FaviconDriver,
                          favicon::FaviconDriverObserver>
      favicon_observation_{this};
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
    : data_(*AppData::Get()),
      contexts_(*data_, data_->store().model()),
      app_icons_(std::make_unique<AppIconImages>()),
      version_(base::UTF8ToUTF16(PaneVersion())) {
  // The design's colors, and the Theme the settings hold, before the window
  // draws: settings.json is read before the lock, as the Swift's was.
  AddPaneColorMixer();
  applied_theme_ = model().settings().theme;
  ApplyTheme(*applied_theme_);
  // The Dock shows the App icon from the start, and Settings' row has it to
  // show the first time.
  dock_icon_ = std::make_unique<DockIcon>(*app_icons_);
  dock_icon_->Show(model().settings().app_icon);
  app_icons_->Load({model().settings().app_icon});
  // A scenario's copy shares the user's preferences, so its window neither
  // reads nor saves the user's frame: 1280 × 800 at every launch (B0).
  window_ = std::make_unique<Window>(
      this,
      /*keeps_frame=*/!base::CommandLine::ForCurrentProcess()->HasSwitch(
          "use-mock-keychain"));
  dialogs_ = std::make_unique<PageDialogs>(this);
  context_menus_ = std::make_unique<PageContextMenus>();
  model_observation_.Observe(&model());
  favicons_observation_.Observe(&favicons_);
  sleep_ = std::make_unique<SleepController>(model(), *this);
  sidebar_width_ = model().settings().sidebar_width;
  // The menu bar, from the same command table as the window's keys, before
  // the window shows, as the Swift's: the Window menu lists it.
  menu_bar_ = std::make_unique<MenuBar>(*this);
  window_->Open();
  window_->Show(/*activate=*/true);
  // The update check at launch, then every six hours (ADR 0002): the pill
  // shows what it finds, behind the Lock too.
  updater_ = std::make_unique<Updater>(
      PaneVersion(), Updater::Config::FromEnvironment(),
      g_browser_process->shared_url_loader_factory(),
      base::BindRepeating(
          [](base::WeakPtr<UiApp> app) {
            if (app) {
              app->Quit(base::DoNothing());
            }
          },
          weak_factory_.GetWeakPtr()));
  updater_observation_.Observe(updater_.get());
  settings_update_ = SettingsUpdateOf(*updater_);
  updater_->Schedule();
  events_ = AppEvents::Listen(
      base::BindRepeating(&UiApp::ShowWindow, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&UiApp::OpenHandedURLs, weak_factory_.GetWeakPtr()));

  // The channel, in a scenario's run alone (ADR 0115).
  if (std::optional<std::string> directory =
          model::TestHook("PANE_AUTOMATION_DIR")) {
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
  sleep_.reset();
  updater_observation_.Reset();
  updater_.reset();
  menu_bar_.reset();
  // Their pages go with the window's Browsers, as quitting keeps their tabs.
  pages_.clear();
  context_menus_.reset();
  dialogs_.reset();
  window_.reset();
  dock_icon_.reset();
  app_icons_.reset();
}

model::BrowserModel& UiApp::model() {
  return data_->store().model();
}

const model::BrowserModel& UiApp::model() const {
  return data_->store().model();
}

UiApp::Switcher::Switcher() = default;
UiApp::Switcher::Switcher(const Switcher&) = default;
UiApp::Switcher& UiApp::Switcher::operator=(const Switcher&) = default;
UiApp::Switcher::~Switcher() = default;

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

// A PIN set or removed: Settings' Security rows follow.
void UiApp::SecurityChanged() {
  window_->Refresh();
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
  // Nothing of the window's stays over the PIN screen.
  CancelSwitcher();
  CancelQuit();

  std::vector<std::string> profile_ids;
  for (const model::BrowserProfile& profile : model().profiles()) {
    profile_ids.push_back(profile.id);
  }
  for (const std::string& profile_id : profile_ids) {
    UnloadProfile(profile_id);
  }
  // What changed is written, and the profiles leave memory, what their pages
  // showed with them.
  data_->store().UnloadProfiles();
  favicons_.Clear();
  favicons_warmed_ = false;

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
  if (!InteractionAllowed()) {
    return std::string();
  }
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
  if (!InteractionAllowed() || !model().FindProfile(profile_id)) {
    return std::string();
  }
  ExpandProfile(profile_id);
  const std::string tab_id = NewTabId();
  model().InsertTab(
      {.id = tab_id, .url = url ? url->possibly_invalid_spec() : NewTabURL()},
      profile_id);
  if (!url) {
    focuses_address_ = tab_id;
  }
  Select(tab_id);
  // A tab just opened at an address loads; it wakes from nothing.
  if (waking_ && waking_->tab_id == tab_id) {
    waking_.reset();
  }
  return tab_id;
}

void UiApp::Select(const std::string& tab_id) {
  const model::Tab* tab = model().FindTab(tab_id);
  const std::optional<std::string> profile_id = model().ProfileIdOfTab(tab_id);
  if (!InteractionAllowed() || !tab || !profile_id) {
    return;
  }
  // The frame that answers the choice, a row's click as a command's, is asked
  // for once it is handled.
  FrameHold hold(window_->compositor());
  // Another tab chosen before the one woken painted: its time is no wake's.
  if (waking_ && waking_->tab_id != tab_id) {
    waking_.reset();
  }
  // ⌘T's tab is timed as a new tab (NewTab).
  const bool new_tab = focuses_address_ == tab_id;
  const bool another = model().active_tab_id() != tab_id;
  if (another && !new_tab) {
    LogInteraction(Interaction::kSelectTab, window_->compositor());
  }
  model().SelectTab(tab_id);
  model().SetPage(model::BrowserModel::Page::kBrowser);
  model().SetExpanded(*profile_id, true);
  if (another) {
    ShowLoadOf(tab_id);
  }

  // The window's one WebView swaps the page left for the one entered in the
  // frame that answers the input.
  if (content::WebContents* page = PageOf(tab_id)) {
    Show(page);
    return;
  }

  // The engine makes the page after the frame that answers the input (11 ms
  // for a new tab's): the row selected, and a new tab's address field, don't
  // wait for it. A tab woken, timed until its page first paints.
  if (!new_tab) {
    waking_ =
        Waking{.tab_id = tab_id,
               .interval = BeginInteractionInterval(Interaction::kWakeTab)};
  }
  window_->ShowNoPage();
  // The tab left's dialog waits for it.
  dialogs_->Update();
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
                      // The profile would not open: the sad tab, whose
                      // Reload tries again.
                      if (!profile) {
                        app->crashed_tab_ids_.insert(tab_id);
                        app->window_->Refresh();
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
    url = GURL(NewTabURL());
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
  auto page = std::make_unique<Page>(this, tab_id, contents);
  const gfx::Image favicon = page->Favicon();
  pages_[tab_id] = std::move(page);
  // A page of its own again, its profile open: its row brightens.
  crashed_tab_ids_.erase(tab_id);
  if (!favicon.IsEmpty()) {
    PageFaviconChanged(tab_id, favicon);
  }
  window_->Refresh();
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
  // Its dialog shows over it, taking the keys, and another tab's waits.
  dialogs_->Update();
  // Chrome's empty page in a discarded one's place loads as it shows.
  if (const auto page = pages_.find(tab_id); page != pages_.end()) {
    page->second->LoadIfDiscarded();
  }
}

void UiApp::Release(const std::string& tab_id) {
  const auto found = pages_.find(tab_id);
  if (found == pages_.end()) {
    return;
  }
  content::WebContents* contents = found->second->web_contents();
  // Unmapped first: its removal is no tab closing.
  pages_.erase(found);
  // A released tab's next page starts over; its row dims.
  crashes_.Forget(tab_id);
  crashed_tab_ids_.erase(tab_id);
  window_->Refresh();
  if (window_->selected() == contents) {
    window_->ClearPage();
  }
  // The selected tab's page went with its load.
  if (model().active_tab_id() == tab_id) {
    ShowLoadOf(tab_id);
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
  window_->ShowNoPage();
  ShowLoadOf(std::nullopt);
  model().SelectTab(std::nullopt);
  model().SetActiveProfile(profile_id);
}

void UiApp::ExpandProfile(const std::string& profile_id) {
  model().SetExpanded(profile_id, true);
}

// The sidebar header's Collapse all: every expanded profile collapses, its
// pages unloading (the Swift's collapseAll).
void UiApp::CollapseAll() {
  if (!InteractionAllowed()) {
    return;
  }
  const std::set<std::string> expanded = model().expanded_profile_ids();
  for (const std::string& profile_id : expanded) {
    CollapseProfile(profile_id);
  }
}

// The toolbar's Back and Forward: the selected tab's history, a step.
void UiApp::Back() {
  if (!InteractionAllowed()) {
    return;
  }
  const std::optional<std::string>& active = model().active_tab_id();
  content::WebContents* page = active ? PageOf(*active) : nullptr;
  if (page && page->GetController().CanGoBack()) {
    page->GetController().GoBack();
  }
}

void UiApp::Forward() {
  if (!InteractionAllowed()) {
    return;
  }
  const std::optional<std::string>& active = model().active_tab_id();
  content::WebContents* page = active ? PageOf(*active) : nullptr;
  if (page && page->GetController().CanGoForward()) {
    page->GetController().GoForward();
  }
}

// A crashed tab comes back; one with no page is selected again, which makes
// it; any other reloads, asking before it posts a form again.
void UiApp::Reload() {
  const std::optional<std::string> active = model().active_tab_id();
  if (!active || !InteractionAllowed()) {
    return;
  }
  if (crashed_tab_ids_.contains(*active)) {
    ReloadCrashed(*active);
    return;
  }
  content::WebContents* page = PageOf(*active);
  if (!page) {
    Select(*active);
    return;
  }
  page->GetController().Reload(content::ReloadType::NORMAL,
                               /*check_for_repost=*/true);
}

void UiApp::Stop() {
  if (!InteractionAllowed()) {
    return;
  }
  const std::optional<std::string>& active = model().active_tab_id();
  if (content::WebContents* page = active ? PageOf(*active) : nullptr) {
    page->Stop();
  }
}

void UiApp::ReloadCrashed(const std::string& tab_id) {
  if (!InteractionAllowed()) {
    return;
  }
  crashed_tab_ids_.erase(tab_id);
  crashes_.Forget(tab_id);
  window_->Refresh();
  if (content::WebContents* page = PageOf(tab_id)) {
    page->GetController().Reload(content::ReloadType::NORMAL,
                                 /*check_for_repost=*/true);
  } else if (model().active_tab_id() == tab_id) {
    Select(tab_id);
  }
}

AppIconImages& UiApp::AppIcons() {
  return *app_icons_;
}

model::ToolbarProgress& UiApp::LoadProgress() {
  return progress_;
}

std::optional<double> UiApp::LoadingOf(const std::string& tab_id) const {
  const auto page = pages_.find(tab_id);
  return page == pages_.end() ? std::nullopt : page->second->loading();
}

void UiApp::ShowLoadOf(const std::optional<std::string>& tab_id) {
  progress_.set_reduces_motion(motion::IsReduced());
  progress_.TabChanged(tab_id ? LoadingOf(*tab_id) : std::nullopt,
                       base::TimeTicks::Now());
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
  if (!InteractionAllowed()) {
    return;
  }
  if (model().expanded_profile_ids().contains(profile_id)) {
    CollapseProfile(profile_id);
  } else {
    ExpandProfile(profile_id);
  }
}

void UiApp::CollapseProfile(const std::string& profile_id) {
  if (!InteractionAllowed()) {
    return;
  }
  model().SetExpanded(profile_id, false);
  UnloadProfile(profile_id);
}

void UiApp::ShowPage(model::BrowserModel::Page page) {
  if (!InteractionAllowed()) {
    return;
  }
  const bool settings = page == model::BrowserModel::Page::kSettings;
  if (settings && model().page() != page) {
    LogInteraction(Interaction::kOpenSettings, window_->compositor());
    // Every sky's tile, decoded before the picker can open.
    app_icons_->LoadAll();
  }
  // The picker is Settings'.
  if (!settings) {
    app_icon_picker_ = false;
  }
  model().SetPage(page);
  RootView* root = window_->root();
  if (!root) {
    return;
  }
  // Back on the browser, its page takes the keys again; Settings takes them
  // in its place.
  if (settings) {
    root->FocusSettings();
  } else if (page == model::BrowserModel::Page::kBrowser) {
    root->FocusPage();
  }
}

// A drag's drop, as the Swift's moveProfile and moveTab: the model's order,
// saved as any change is (BrowserModel::ScheduleSave).
void UiApp::MoveProfile(int from, int to) {
  if (InteractionAllowed()) {
    model().MoveProfile(from, to);
  }
}

void UiApp::MoveTab(const std::string& profile_id, int from, int to) {
  if (InteractionAllowed()) {
    model().MoveTab(profile_id, from, to);
  }
}

void UiApp::DownloadUpdate() {
  updater_->DownloadUpdate();
}

void UiApp::DismissUpdate() {
  updater_->DismissPill();
}

// Settings' update row, as the app menu's Check for Updates…, behind the Lock
// too.
void UiApp::CheckForUpdates() {
  updater_->Check();
}

void UiApp::EditSettings(
    base::RepeatingCallback<void(model::AppSettings&)> edit) {
  if (InteractionAllowed()) {
    ChangeSettings([&](model::AppSettings& settings) { edit.Run(settings); });
  }
}

void UiApp::OpenAppIconPicker() {
  if (!InteractionAllowed() || app_icon_picker_ ||
      model().page() != model::BrowserModel::Page::kSettings) {
    return;
  }
  LogInteraction(Interaction::kOpenAppIconPicker, window_->compositor());
  app_icon_picker_ = true;
  window_->Refresh();
}

// A sky applies at once, and the Dock follows; the picker stays open to
// compare.
void UiApp::ChooseAppIcon(model::AppIcon icon) {
  if (!InteractionAllowed()) {
    return;
  }
  LogInteraction(Interaction::kChooseAppIcon, window_->compositor());
  ChangeSettings(
      [&](model::AppSettings& settings) { settings.app_icon = icon; });
}

void UiApp::CloseAppIconPicker() {
  if (std::exchange(app_icon_picker_, false)) {
    window_->Refresh();
  }
}

void UiApp::ShowPinScreen(model::PinEntry::Mode mode) {
  if (InteractionAllowed()) {
    data_->ShowPinScreen(mode);
  }
}

void UiApp::ToggleSettings() {
  // ShowPage asks InteractionAllowed().
  ShowPage(model().page() == model::BrowserModel::Page::kSettings
               ? model::BrowserModel::Page::kBrowser
               : model::BrowserModel::Page::kSettings);
}

// A loaded tab goes once its page agrees to close (beforeunload may keep it);
// the strip's removal then takes it from the model (TabRemoved). One without
// a page goes at once.
void UiApp::CloseTab(const std::string& tab_id) {
  if (!InteractionAllowed() || !model().FindTab(tab_id)) {
    return;
  }
  if (content::WebContents* page = PageOf(tab_id)) {
    BrowserWindowInterface* browser =
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(page);
    TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
    const int index =
        strip ? strip->GetIndexOfWebContents(page) : TabStripModel::kNoTab;
    if (index != TabStripModel::kNoTab) {
      // No historical tab: Pane's Reopen closed tab keeps its own list, and
      // tab restore's names every page closed (ADR 0121).
      strip->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
      return;
    }
  }
  RemoveTabAndReselect(tab_id);
}

void UiApp::RemoveTabAndReselect(const std::string& tab_id) {
  const bool was_active = model().active_tab_id() == tab_id;
  const std::optional<std::string> profile_id = model().ProfileIdOfTab(tab_id);
  crashes_.Forget(tab_id);
  model().RemoveTab(tab_id);
  if (was_active) {
    SelectAfterClosing(profile_id);
  }
}

void UiApp::NewTabIn(const std::string& profile_id) {
  if (InteractionAllowed() && model().FindProfile(profile_id)) {
    OpenTab(profile_id, std::nullopt);
  }
}

void UiApp::EditProfile(const std::string& profile_id) {
  PresentProfileSheet();
}

void UiApp::NewProfile() {
  PresentProfileSheet();
}

void UiApp::PresentProfileSheet() {
  if (RootView* root = window_->root(); root && InteractionAllowed()) {
    root->ShowSheet(true);
  }
}

void UiApp::SetKeepLoaded(const std::string& profile_id, bool keep_loaded) {
  if (!InteractionAllowed()) {
    return;
  }
  model().UpdateProfile(profile_id, [&](model::BrowserProfile& profile) {
    profile.keep_loaded = keep_loaded;
  });
}

// Removed from the model at once and recorded in deletions.json before the
// engine starts on its data, so a quit, a crash, or a lock on the way leaves
// its folder to the next open (ADR 0020). A profile not open this session
// opens first, so the data of earlier ones goes too.
void UiApp::DeleteProfile(const std::string& profile_id) {
  if (!InteractionAllowed() || !model().FindProfile(profile_id)) {
    return;
  }
  UnloadProfile(profile_id);
  model().RemoveProfile(profile_id);
  deleting_.insert(profile_id);
  data_->RecordDeletion(
      profile_id,
      base::BindOnce(
          [](base::WeakPtr<UiApp> app, std::string profile_id) {
            if (!app) {
              return;
            }
            app->contexts_.Delete(
                profile_id,
                base::BindOnce(&UiApp::ProfileDeleted,
                               app->weak_factory_.GetWeakPtr(), profile_id));
          },
          weak_factory_.GetWeakPtr(), profile_id));
}

void UiApp::ProfileDeleted(const std::string& profile_id, bool removes_folder) {
  deleting_.erase(profile_id);
  if (removes_folder) {
    data_->RemoveDeleted(profile_id);
  }
}

// Each step of a drag of the sidebar's edge lays the window out at the new
// width; settings.json waits for the drag to end.
void UiApp::ResizeSidebar(double width) {
  if (!InteractionAllowed()) {
    return;
  }
  sidebar_width_ = model::ClampedSidebarWidth(width);
  window_->SetSidebarWidth(sidebar_width_);
}

void UiApp::EndSidebarResize() {
  if (!InteractionAllowed()) {
    return;
  }
  ChangeSettings([&](model::AppSettings& settings) {
    settings.sidebar_width = sidebar_width_;
  });
}

void UiApp::ResetSidebarWidth() {
  // Each asks InteractionAllowed().
  ResizeSidebar(model::kSidebarWidth);
  EndSidebarResize();
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

void UiApp::PageFaviconChanged(const std::string& tab_id,
                               const gfx::Image& image) {
  favicons_.Update(tab_id, image,
                   base::BindOnce(
                       [](base::WeakPtr<UiApp> app, std::string tab_id,
                          const std::string& data_url) {
                         if (app) {
                           app->model().UpdateTab(tab_id, [&](model::Tab& tab) {
                             tab.favicon = data_url;
                           });
                         }
                       },
                       weak_factory_.GetWeakPtr(), tab_id));
  window_->Refresh();
}

void UiApp::PageNavigated(const std::string& tab_id) {
  if (content::WebContents* contents = PageOf(tab_id);
      contents && window_->selected() == contents) {
    window_->Refresh();
  }
}

// The selected tab's load moves the bar (the Swift's didChangeLoading); its
// start and end also turn Reload into Stop and back.
void UiApp::PageLoadingChanged(const std::string& tab_id,
                               std::optional<double> from,
                               std::optional<double> to) {
  if (model().active_tab_id() != tab_id || !PageOf(tab_id)) {
    return;
  }
  progress_.set_reduces_motion(motion::IsReduced());
  progress_.LoadingChanged(from, to, base::TimeTicks::Now());
  if (from.has_value() != to.has_value()) {
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
  window_->Refresh();
}

// The first crash reloads the page, as a slept tab wakes. A page that dies
// again within a minute stays crashed instead of looping, as does one behind
// another tab, until it reloads (the Swift's rendererDidTerminate). Only a
// renderer Chrome would show its sad tab for counts: not one that ended
// normally, nor a page reloaded meanwhile.
void UiApp::PageCrashed(const std::string& tab_id) {
  content::WebContents* page = PageOf(tab_id);
  const bool active = model().active_tab_id() == tab_id;
  if (page && active) {
    progress_.Crashed(base::TimeTicks::Now());
  }
  if (page && page->IsCrashed()) {
    if (crashes_.Crashed(tab_id, base::Time::Now()) || !active) {
      // The sad tab, in the selected tab's place or once it is selected.
      crashed_tab_ids_.insert(tab_id);
    } else {
      page->GetController().Reload(content::ReloadType::NORMAL,
                                   /*check_for_repost=*/true);
    }
  }
  window_->Refresh();
}

// A crashed tab reloaded starts over, as the Swift's reloadCrashed: its next
// crash is a first one. The automatic reload of a first crash, never marked,
// keeps the loop's memory.
void UiApp::PageCameBack(const std::string& tab_id) {
  if (crashed_tab_ids_.erase(tab_id)) {
    crashes_.Forget(tab_id);
    window_->Refresh();
  }
}

// The selected tab shows Chrome's empty page where the old one was, in the
// same turn, so no frame shows the place empty, and loads it on the next, as
// a page's own calls wait for (ADR 0017). Another waits for its selection.
void UiApp::PageReplaced(const std::string& tab_id) {
  window_->Refresh();
  content::WebContents* page = PageOf(tab_id);
  if (!page || model().active_tab_id() != tab_id || !InteractionAllowed()) {
    return;
  }
  window_->ShowTab(page, /*chosen=*/false);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<UiApp> app, std::string tab_id) {
                       if (!app) {
                         return;
                       }
                       if (const auto page = app->pages_.find(tab_id);
                           page != app->pages_.end()) {
                         page->second->LoadIfDiscarded();
                       }
                     },
                     weak_factory_.GetWeakPtr(), tab_id));
}

void UiApp::PageFirstPaint(const std::string& tab_id) {
  if (waking_ && waking_->tab_id == tab_id) {
    EndInteractionAtNextFrame(Interaction::kWakeTab,
                              std::exchange(waking_, std::nullopt)->interval,
                              window_->compositor());
  }
}

void UiApp::SleepTab(const std::string& tab_id) {
  Release(tab_id);
}

// MARK: PageDialogs::Delegate

RootView* UiApp::DialogRoot() {
  return window_ ? window_->root() : nullptr;
}

content::WebContents* UiApp::PageForDialogs() {
  if (!InteractionAllowed() || !window_->root()) {
    return nullptr;
  }
  const std::optional<std::string>& active = model().active_tab_id();
  content::WebContents* page = active ? PageOf(*active) : nullptr;
  // The page the window shows, not one still being made.
  return page && window_->selected() == page ? page : nullptr;
}

// MARK: The tab switcher

// The open profiles' tabs, by recent use: a collapsed profile's are left out,
// as the Swift's were.
void UiApp::SwitchTab(bool backward) {
  if (!InteractionAllowed()) {
    return;
  }
  if (switcher_) {
    int count = 0;
    for (const model::SwitcherGroup& group : switcher_->groups) {
      count += static_cast<int>(group.tabs.size());
    }
    switcher_->selected =
        model::SwitcherCycle(switcher_->selected, backward ? -1 : 1, count);
    if (RootView* root = window_->root()) {
      root->SelectInSwitcher(switcher_->selected);
    }
    return;
  }

  const std::set<std::string>& expanded = model().expanded_profile_ids();
  std::set<std::string> open_tabs;
  for (const model::BrowserProfile& profile : model().profiles()) {
    if (expanded.contains(profile.id)) {
      for (const model::Tab& tab : profile.tabs) {
        open_tabs.insert(tab.id);
      }
    }
  }
  const std::vector<std::string>& mru = model().history().mru();
  std::vector<std::string> recent;
  for (const std::string& tab_id : mru) {
    if (open_tabs.contains(tab_id)) {
      recent.push_back(tab_id);
    }
  }
  Switcher switcher;
  switcher.groups = model::SwitcherGroups(recent, model().profiles());
  std::vector<model::SwitcherTab> flattened;
  for (const model::SwitcherGroup& group : switcher.groups) {
    flattened.insert(flattened.end(), group.tabs.begin(), group.tabs.end());
  }
  RootView* root = window_->root();
  if (flattened.empty() || !root) {
    return;
  }
  LogInteraction(Interaction::kOpenSwitcher, window_->compositor());
  switcher.selected = model::SwitcherInitialSelectedIndex(
      flattened, mru.size() > 1 ? std::optional(mru[1]) : std::nullopt);
  if (backward) {
    switcher.selected =
        model::SwitcherCycle(0, -1, static_cast<int>(flattened.size()));
  }
  switcher_ = std::move(switcher);
  root->ShowSwitcher(SwitcherGroupsToShow(), switcher_->selected);
}

void UiApp::CommitSwitcher() {
  if (!switcher_) {
    return;
  }
  std::optional<std::string> chosen;
  int index = 0;
  for (const model::SwitcherGroup& group : switcher_->groups) {
    for (const model::SwitcherTab& tab : group.tabs) {
      if (index++ == switcher_->selected) {
        chosen = tab.id;
      }
    }
  }
  CancelSwitcher();
  if (chosen) {
    Select(*chosen);
  }
}

void UiApp::CancelSwitcher() {
  if (!std::exchange(switcher_, std::nullopt)) {
    return;
  }
  if (RootView* root = window_->root()) {
    root->HideSwitcher();
  }
}

std::vector<TabSwitcherView::Group> UiApp::SwitcherGroupsToShow() {
  std::vector<TabSwitcherView::Group> groups;
  if (!switcher_) {
    return groups;
  }
  for (const model::SwitcherGroup& group : switcher_->groups) {
    TabSwitcherView::Group shown;
    shown.name = base::UTF8ToUTF16(group.name);
    shown.color = ColorOf(group.color);
    for (const model::SwitcherTab& tab : group.tabs) {
      shown.tabs.push_back({.id = tab.id,
                            .title = base::UTF8ToUTF16(tab.title),
                            .favicon = favicons_.Image(
                                tab.id, tab.favicon.value_or(std::string()))});
    }
    groups.push_back(std::move(shown));
  }
  return groups;
}

// MARK: Window::Controller

// What the sidebar shows, from the model: no profile shows as active, and
// no tab as selected, off the browser page (ADR 0064).
SidebarContent UiApp::Sidebar() {
  const model::BrowserModel& model = this->model();
  const bool browser = model.page() == model::BrowserModel::Page::kBrowser;
  SidebarContent content;
  content.settings_shown = model.page() == model::BrowserModel::Page::kSettings;
  if (updater_ && updater_->ShowsPill()) {
    content.update = SidebarUpdate{
        .label = updater_->Label(),
        .downloading = updater_->status() == Updater::Status::kDownloading,
        .progress = updater_->download_progress().value_or(0),
        .downloaded = updater_->status() == Updater::Status::kDownloaded,
    };
  }
  for (const model::BrowserProfile& profile : model.profiles()) {
    SidebarProfile row;
    row.id = profile.id;
    row.name = base::UTF8ToUTF16(profile.name);
    row.color = ColorOf(profile.color);
    row.shown = browser && model.active_profile_id() == profile.id;
    row.expanded = model.expanded_profile_ids().contains(profile.id);
    if (const auto status = model.proxy_status().find(profile.id);
        status != model.proxy_status().end()) {
      row.proxy = status->second;
    }
    row.tab_count = static_cast<int>(profile.tabs.size());
    row.keep_loaded = profile.keep_loaded;
    if (row.expanded) {
      for (const model::Tab& tab : profile.tabs) {
        SidebarTab tab_row;
        tab_row.id = tab.id;
        tab_row.title = base::UTF8ToUTF16(tab.title);
        tab_row.favicon = favicons_.Image(tab.id, tab.favicon);
        tab_row.selected = row.shown && model.active_tab_id() == tab.id;
        tab_row.loaded = PageOf(tab.id) != nullptr;
        row.tabs.push_back(std::move(tab_row));
      }
    }
    content.profiles.push_back(std::move(row));
  }
  return content;
}

RootView::Shows UiApp::Shows() {
  const model::BrowserModel& model = this->model();
  const bool browser = model.page() == model::BrowserModel::Page::kBrowser;
  RootView::Shows shows;
  const std::optional<std::string>& selected = model.active_tab_id();
  shows.content = model.page() == model::BrowserModel::Page::kSettings
                      ? RootView::Content::kSettings
                  : !browser  ? RootView::Content::kEmpty
                  : !selected ? RootView::Content::kEmpty
                  : crashed_tab_ids_.contains(*selected)
                      ? RootView::Content::kSadTab
                      : RootView::Content::kPage;
  shows.sidebar_width = sidebar_width_;
  shows.can_collapse_all = !model.expanded_profile_ids().empty();
  shows.settings.settings = model.settings();
  shows.settings.has_pin = data_->HasPin();
  shows.settings.update = settings_update_;
  shows.settings.version = version_;
  shows.app_icon_picker =
      app_icon_picker_ && model.page() == model::BrowserModel::Page::kSettings;

  // The selected tab's toolbar, on the browser page: off it, the toolbar
  // goes, and coming back shows the tab's address again, as the Swift's
  // toolbar, made again, did.
  ToolbarView::Shows& toolbar = shows.toolbar;
  const std::optional<std::string>& active = model.active_tab_id();
  if (const model::Tab* tab =
          browser && active ? model.FindTab(*active) : nullptr) {
    toolbar.tab_id = tab->id;
    toolbar.url = base::UTF8ToUTF16(tab->url);
    toolbar.title = base::UTF8ToUTF16(tab->title);
    if (content::WebContents* page = PageOf(tab->id)) {
      toolbar.can_go_back = page->GetController().CanGoBack();
      toolbar.can_go_forward = page->GetController().CanGoForward();
      toolbar.loading = LoadingOf(tab->id).has_value();
    }
  }
  if (const std::optional<std::string>& profile_id = model.active_profile_id();
      browser && profile_id) {
    if (const model::BrowserProfile* profile = model.FindProfile(*profile_id)) {
      toolbar.profile_name = base::UTF8ToUTF16(profile->name);
      toolbar.profile_color = ColorOf(profile->color);
    }
  }
  return shows;
}

// The window's pages went with it (PageDestroyed); their tabs stay, and the
// selected one's page is made again when the window shows.
void UiApp::WindowClosed() {
  focuses_address_.reset();
  switcher_.reset();
  quit_confirming_ = false;
  // A dialog shown goes back in line, for the window opened again.
  dialogs_->Update();
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
    if (RootView* root = window_->root()) {
      root->FocusPage();
    }
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

// Esc in the address: the page takes the keys (the Swift's onExitCommand).
void UiApp::AddressLeft() {
  if (!InteractionAllowed()) {
    return;
  }
  if (RootView* root = window_->root()) {
    root->FocusPage();
  }
}

// ⌘T: a new tab of the profile shown, at the start page.
void UiApp::NewTab() {
  if (!InteractionAllowed()) {
    return;
  }
  LogInteraction(Interaction::kNewTab, window_->compositor());
  std::optional<std::string> profile_id = model().active_profile_id();
  if (!profile_id && !model().profiles().empty()) {
    profile_id = model().profiles().front().id;
  }
  if (profile_id) {
    OpenTab(*profile_id, std::nullopt);
  }
}

void UiApp::FocusAddressField() {
  if (InteractionAllowed()) {
    window_->FocusAddress();
  }
}

// ⌘1 to ⌘8: the tab at that place, or nothing when the profile has fewer,
// as the Swift's selectTab(atIndex:). ⌘9: the last, as Chrome's and
// Safari's, by the user's call (2026-11-10), where the Swift's was the
// ninth.
void UiApp::SelectTabAt(int number) {
  if (!InteractionAllowed()) {
    return;
  }
  const std::optional<std::string>& profile_id = model().active_profile_id();
  const model::BrowserProfile* profile =
      profile_id ? model().FindProfile(*profile_id) : nullptr;
  if (!profile || profile->tabs.empty() || number < 1) {
    return;
  }
  if (number == 9) {
    Select(profile->tabs.back().id);
    return;
  }
  if (number > static_cast<int>(profile->tabs.size())) {
    return;
  }
  Select(profile->tabs[number - 1].id);
}

// MARK: Quit Pane? (ADR 0053)

void UiApp::ConfirmQuit() {
  RootView* root = window_->root();
  // From Quit Pane? (⌘Q again), or at the PIN screen, where nothing is open
  // to lose; with no window, there is nowhere to ask.
  if (quit_confirming_ || data_->IsLocked() || !root) {
    QuitNow();
    return;
  }
  quit_confirming_ = true;
  LogInteraction(Interaction::kOpenQuitDialog, window_->compositor());
  root->ShowQuitDialog(true);
}

void UiApp::CancelQuit() {
  if (!std::exchange(quit_confirming_, false)) {
    return;
  }
  if (RootView* root = window_->root()) {
    root->ShowQuitDialog(false);
  }
}

void UiApp::QuitNow() {
  quit_confirming_ = false;
  Quit(base::DoNothing());
}

// MARK: The command table (commands.h)

bool UiApp::IsCommandEnabled(Command command) {
  switch (command) {
    // Behind the Lock too, as the Swift's menu let them.
    case Command::kQuit:
    case Command::kCheckForUpdates:
      return true;
    // Only a PIN locks Pane.
    case Command::kLock:
      return InteractionAllowed() && data_->HasPin();
    default:
      return InteractionAllowed();
  }
}

std::optional<std::u16string> UiApp::CommandTitle(Command command) {
  if (command == Command::kFullScreen) {
    return window_->IsFullScreen() ? u"Exit Full Screen" : u"Enter Full Screen";
  }
  return std::nullopt;
}

void UiApp::RunCommand(Command command) {
  if (!IsCommandEnabled(command)) {
    return;
  }
  // The frame that answers the command is asked for once it is handled.
  FrameHold hold(window_->compositor());
  const std::optional<std::string>& active = model().active_tab_id();
  switch (command) {
    case Command::kCheckForUpdates:
      CheckForUpdates();
      return;
    case Command::kSettings:
      ShowPage(model::BrowserModel::Page::kSettings);
      return;
    case Command::kLock:
      LockNow();
      return;
    case Command::kQuit:
      ConfirmQuit();
      return;
    case Command::kNewTab:
      NewTab();
      return;
    case Command::kCloseTab:
      if (active) {
        CloseTab(*active);
      }
      return;
    case Command::kReopenClosedTab:
      ReopenClosedTab();
      return;
    case Command::kPrint:
      PrintPage();
      return;
    case Command::kReload:
      Reload();
      return;
    case Command::kActualSize:
    case Command::kZoomIn:
    case Command::kZoomOut:
      ZoomPage(command);
      return;
    case Command::kFullScreen:
      window_->ToggleFullScreen();
      return;
    case Command::kBack:
      Back();
      return;
    case Command::kForward:
      Forward();
      return;
    case Command::kFocusAddress:
      FocusAddressField();
      return;
    case Command::kNextTab:
      SwitchTab(/*backward=*/false);
      return;
    case Command::kPreviousTab:
      SwitchTab(/*backward=*/true);
      return;
    case Command::kFind:
      ShowFind();
      return;
    case Command::kDeveloperTools:
      ToggleDevTools();
      return;
    // The Extensions page is X4's (chrome://extensions in the profile's tab,
    // D3): its item runs nothing until then.
    case Command::kExtensions:
      return;
    default:
      if (const int number = TabNumberOf(command)) {
        SelectTabAt(number);
      }
      // The platform's own run where the platform sends them (menu_bar.h).
      return;
  }
}

// MARK: Find in page (ADR 0067)

void UiApp::ShowFind() {
  content::WebContents* page = window_->selected();
  if (!InteractionAllowed() || !page ||
      model().page() != model::BrowserModel::Page::kBrowser) {
    return;
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(page);
  FindBarController* controller = FindBarController::From(browser);
  if (!controller) {
    return;
  }
  LogInteraction(Interaction::kShowFindBar, window_->compositor());
  // Made the first time, for the tab the Browser shows: the window's bar.
  controller->find_bar();
  if (FindInPage* find = FindInPage::From(browser)) {
    find->Open();
  }
}

void UiApp::Find(const std::u16string& text) {
  if (FindInPage* find =
          InteractionAllowed() ? window_->ShownFind() : nullptr) {
    find->Find(text);
  }
}

void UiApp::FindStep(bool forward) {
  if (FindInPage* find =
          InteractionAllowed() ? window_->ShownFind() : nullptr) {
    find->Step(forward);
  }
}

void UiApp::CloseFind() {
  if (FindInPage* find = window_->ShownFind()) {
    find->Close();
  }
}

// MARK: The page's context menu (U2) and DevTools (ADR 0024)

// The selected tab's page alone shows its menu, as the Swift's did.
void UiApp::ShowContextMenu(content::RenderFrameHost& frame,
                            const content::ContextMenuParams& params) {
  content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(&frame);
  if (!InteractionAllowed() || !contents || contents != window_->selected()) {
    return;
  }
  context_menus_->Show(frame, params);
}

void UiApp::ToggleDevTools() {
  content::WebContents* page = window_->selected();
  if (!InteractionAllowed() || !page) {
    return;
  }
  ::pane::ToggleDevTools(
      page, base::BindRepeating(
                [](base::WeakPtr<UiApp> app) -> ::ui::Compositor* {
                  return app ? app->window_->compositor() : nullptr;
                },
                weak_factory_.GetWeakPtr()));
}

void UiApp::ReopenClosedTab() {
  std::optional<model::ClosedTab> closed = model().PopClosedTab();
  if (!closed || !model().FindProfile(closed->profile_id)) {
    return;
  }
  const GURL url(closed->url);
  OpenTab(closed->profile_id,
          url.is_valid() ? std::optional<GURL>(url) : std::nullopt);
}

void UiApp::ZoomPage(Command command) {
  const std::optional<std::string>& active = model().active_tab_id();
  content::WebContents* page = active ? PageOf(*active) : nullptr;
  if (!page) {
    return;
  }
  auto* controller = zoom::ZoomController::FromWebContents(page);
  const double before = controller ? controller->GetZoomLevel() : 0;
  const content::PageZoom zoom =
      command == Command::kZoomIn    ? content::PAGE_ZOOM_IN
      : command == Command::kZoomOut ? content::PAGE_ZOOM_OUT
                                     : content::PAGE_ZOOM_RESET;
  zoom::PageZoom::Zoom(page, zoom);
  // The Swift's engine's names, which shortcuts reads.
  PANE_TRACE("call",
             command == Command::kZoomIn    ? "PaneWebContents.zoomIn"
             : command == Command::kZoomOut ? "PaneWebContents.zoomOut"
                                            : "PaneWebContents.resetZoom",
             base::DictValue()
                 .Set("before", before)
                 .Set("after", controller ? controller->GetZoomLevel() : 0));
}

void UiApp::PrintPage() {
  const std::optional<std::string>& active = model().active_tab_id();
  if (content::WebContents* page = active ? PageOf(*active) : nullptr) {
    PANE_TRACE("call", "PaneWebContents.print");
    printing::StartPrint(page, /*print_preview_disabled=*/true,
                         /*has_selection=*/false);
  }
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
  // The saved favicons decode as the profiles load, before a profile expands
  // to show them.
  if (!favicons_warmed_ && !model().profiles().empty()) {
    favicons_warmed_ = true;
    std::vector<std::pair<std::string, std::string>> saved;
    for (const model::BrowserProfile& profile : model().profiles()) {
      for (const model::Tab& tab : profile.tabs) {
        saved.emplace_back(tab.id, tab.favicon);
      }
    }
    favicons_.Warm(saved);
  }
  window_->Refresh();
}

void UiApp::OnSettingsChanged() {
  // A Theme chosen reaches the app, its menus, and its pages at once (ADR
  // 0001), timed to the frame that shows it.
  const model::AppSettings& settings = model().settings();
  if (settings.theme != applied_theme_) {
    LogInteraction(Interaction::kChangeTheme, window_->compositor());
    applied_theme_ = settings.theme;
    ApplyTheme(settings.theme);
  }
  // An App icon reaches the Dock (ADR 0089); Settings shows every change.
  // Sleep, Unload, and Trim are the scheduler's at its next tick.
  dock_icon_->Show(settings.app_icon);
  window_->Refresh();
}

void UiApp::OnExpandedProfilesChanged() {
  window_->Refresh();
}

// The sleep clocks follow the selection: the tab and profile left start
// theirs.
void UiApp::OnActiveTabChanged() {
  sleep_->SelectionChanged();
  window_->Refresh();
}

void UiApp::OnActiveProfileChanged() {
  sleep_->SelectionChanged();
  window_->Refresh();
}

void UiApp::OnProxyStatusChanged() {
  window_->Refresh();
}

void UiApp::OnPageChanged() {
  window_->Refresh();
}

void UiApp::OnFaviconChanged(const std::string& tab_id) {
  window_->Refresh();
}

void UiApp::OnUpdaterChanged() {
  // Written once a change, not at each refresh: its time takes the system's
  // formatter.
  settings_update_ = SettingsUpdateOf(*updater_);
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
              .Set("loading", LoadingOf(tab.id).has_value())
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
  // What keeps a tab from sleeping, as its page has it.
  base::ListValue picture_in_picture;
  base::ListValue devtools;
  for (const auto& [tab_id, page] : pages_) {
    content::WebContents* contents = page->web_contents();
    if (contents->HasPictureInPictureVideo() ||
        contents->HasPictureInPictureDocument()) {
      picture_in_picture.Append(tab_id);
    }
    // DevTools showing for the tab, as the Swift's model had them: docked
    // beside it once their page settled where it docks, or in their own
    // window; not while Chrome is still making them.
    if (DevToolsWindow* window =
            DevToolsWindow::GetInstanceForInspectedWebContents(contents);
        window && (!window->IsDocked() ||
                   DevToolsWindow::GetInTabWebContents(contents, nullptr))) {
      devtools.Append(tab_id);
    }
  }
  // The tab switcher as it shows: each group's profile, name, and tabs.
  base::DictValue switcher;
  switcher.Set("visible", switcher_.has_value());
  switcher.Set("selectedIndex", switcher_ ? switcher_->selected : 0);
  base::ListValue groups;
  if (switcher_) {
    for (const model::SwitcherGroup& group : switcher_->groups) {
      base::ListValue tabs;
      for (const model::SwitcherTab& tab : group.tabs) {
        tabs.Append(tab.id);
      }
      groups.Append(base::DictValue()
                        .Set("profileID", group.id)
                        .Set("name", group.name)
                        .Set("tabs", std::move(tabs)));
    }
  }
  switcher.Set("groups", std::move(groups));

  // The address's load bar as drawn now: started, the share filled, its
  // opacity, and whether the slow mode's shimmer runs.
  const base::TimeTicks now = base::TimeTicks::Now();
  base::DictValue progress_bar =
      base::DictValue()
          .Set("started", progress_.is_started())
          .Set("progress", progress_.progress())
          .Set("alpha", progress_.AlphaAt(now))
          .Set("shimmer", progress_.ShimmerAt(now).has_value());

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
      // What the window shows, its views and the menus it opens: light or
      // dark, as the Theme has it or, under System, the Mac.
      .Set("appearance",
           widget && widget->GetNativeTheme()->preferred_color_scheme() ==
                         ui::NativeTheme::PreferredColorScheme::kDark
               ? "dark"
               : "light")
      .Set("appIcon", model::AppIconName(model.settings().app_icon))
      .Set("dockIcon", dock_icon_ && dock_icon_->shown()
                           ? model::AppIconName(*dock_icon_->shown())
                           : std::string_view())
      .Set("appIconPicker", app_icon_picker_)
      .Set("quitConfirmation", quit_confirming_)
      .Set("sidebarWidth", sidebar_width_)
      .Set("pendingProfileDeletions", static_cast<int>(deleting_.size()))
      // A page's dialog showing, and how many wait their turn.
      .Set("sheet", dialogs_->Shown())
      .Set("pendingRequests", dialogs_->waiting())
      .Set("closedTabs", static_cast<int>(model.history().closed().size()))
      .Set("extensions", std::move(extensions))
      .Set("downloads", std::move(downloads))
      .Set("crashedTabs", std::move(crashed))
      .Set("pictureInPictureTabs", std::move(picture_in_picture))
      .Set("devToolsTabs", std::move(devtools))
      .Set("switcher", std::move(switcher))
      // Find in page's bar, the page shown's (the Swift's WindowState.find).
      .Set("find", FindState(root))
      // The page's menu shown last, until it closes, and how many have.
      .Set("contextMenu", context_menus_->model()
                              ? base::Value(PageContextMenus::Describe(
                                    *context_menus_->model()))
                              : base::Value())
      .Set("contextMenusShown", context_menus_->shown())
      // What the full screen's hint says, while it does.
      .Set("exclusiveAccessHint",
           root && !root->exclusive_access_hint().empty()
               ? base::Value(root->exclusive_access_hint())
               : base::Value())
      .Set("address", root ? root->address() : std::u16string())
      .Set("progressBar", std::move(progress_bar))
      .Set("update", UpdateState(*updater_))
      .Set("toolbar", root ? ToolbarState(*root->toolbar()) : base::DictValue())
      .Set("focus", root ? root->focus() : std::string("none"))
      .Set("pageFullscreen", window_->page_fullscreen())
      .Set("window", WindowState(widget))
      .Set("security", data_->State());
}

}  // namespace pane
