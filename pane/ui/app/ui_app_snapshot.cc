// Copyright 2026 Pane. Licensed under GPL-3.0-only.

// What the app shows, as the automation channel replies with it (ADR 0018):
// its state, read for the scenarios, apart from what the app does.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/browser_context.h"
#include "pane/browser/profiles.h"
#include "pane/model/app_settings.h"
#include "pane/model/profile.h"
#include "pane/ui/app/dock_icon.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/dialogs/extension_prompt_view.h"
#include "pane/ui/extensions/action_popup.h"
#include "pane/ui/extensions/installed_extensions.h"
#include "pane/ui/toolbar/address_field.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace pane {

namespace {

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

// The computer's WebGL strings, as a fingerprint on its platform claims them.
base::Value HostWebGLState() {
  const std::optional<model::Fingerprint::WebGL> webgl = HostWebGL();
  return webgl ? base::Value(base::DictValue()
                                 .Set("vendor", webgl->vendor)
                                 .Set("renderer", webgl->renderer))
               : base::Value();
}

std::string_view PageName(model::BrowserModel::Page page) {
  switch (page) {
    case model::BrowserModel::Page::kBrowser:
      return "browser";
    case model::BrowserModel::Page::kSettings:
      return "settings";
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

// An action's popup as the channel reads it (the Swift's): its extension,
// whether its page is up, and the page's size.
base::Value ExtensionPopupState(const ActionPopup* popup) {
  if (!popup) {
    return base::Value();
  }
  const gfx::Size size = popup->page_size();
  return base::Value(base::DictValue()
                         .Set("extension", popup->extension_id())
                         .Set("open", popup->shown())
                         .Set("width", size.width())
                         .Set("height", size.height())
                         .Set("above", popup->above()));
}

// A prompt asking, as the Swift's channel answered a sheet: its kind, its
// title, what it says, and its buttons, the accept first; and Report abuse,
// ticked or not, as `view` shows it.
base::Value PromptState(const ExtensionPrompt& prompt,
                        const ExtensionPromptView* view) {
  std::vector<std::u16string> lines;
  if (!prompt.permissions_heading.empty()) {
    lines.push_back(prompt.permissions_heading);
  }
  lines.insert(lines.end(), prompt.permissions.begin(),
               prompt.permissions.end());
  return base::Value(base::DictValue()
                         .Set("kind", ExtensionPromptKindName(prompt.kind))
                         .Set("title", prompt.title)
                         .Set("message", base::JoinString(lines, u"\n"))
                         .Set("buttons", base::ListValue()
                                             .Append(prompt.accept_title)
                                             .Append(prompt.cancel_title))
                         .Set("reportAbuse", view && view->report_abuse()));
}

// Find in page's bar as it shows.
base::DictValue FindState(RootView* root) {
  const FindBarView::State state =
      root ? root->find_state() : FindBarView::State();
  return base::DictValue()
      .Set("visible", state.visible)
      .Set("text", state.text)
      .Set("matches", state.matches.value_or(0))
      .Set("activeMatch", state.active);
}

}  // namespace

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
    const BrowserContext* context = contexts_.GetContext(profile.id);
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
            // What the engine has of the choice, while the profile is open.
            .Set("engineSavesHistory",
                 context ? base::Value(context->SavesHistory()) : base::Value())
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
    for (const InstalledExtension& extension :
         InstalledExtensions(open, tab, nullptr)) {
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
  for (const auto& entry : pages_) {
    const std::string& tab_id = entry.first;
    content::WebContents* contents = PageOf(tab_id);
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
      // The profile sheet: "new", or the edited profile's id.
      .Set("profileSheet",
           profile_sheet_ ? base::Value(*profile_sheet_) : base::Value())
      // The Mac's WebGL strings, from Chrome's GPU info.
      .Set("hostWebGL", HostWebGLState())
      .Set("quitConfirmation", quit_confirming_)
      .Set("sidebarWidth", sidebar_width_)
      .Set("pendingProfileDeletions", static_cast<int>(deleting_.size()))
      // An extension's prompt asking, over a page's dialog, else a page's
      // dialog showing, and how many wait their turn.
      .Set("sheet", extensions_->prompt()
                        ? PromptState(*extensions_->prompt(),
                                      root ? root->extension_prompt() : nullptr)
                        : dialogs_->Shown())
      .Set("pendingRequests", dialogs_->waiting())
      .Set("closedTabs", static_cast<int>(model.history().closed().size()))
      .Set("extensions", std::move(extensions))
      // The toolbar's extensions menu, the extension whose more menu shows,
      // and an action's popup while it shows or loads, its page's size.
      .Set("extensionsMenu", extensions_->menu_open())
      .Set("extensionMoreMenu", extensions_->more_menu()
                                    ? base::Value(*extensions_->more_menu())
                                    : base::Value())
      .Set("extensionPopup", ExtensionPopupState(extensions_->popup()))
      .Set("downloads", std::move(downloads))
      .Set("crashedTabs", std::move(crashed))
      .Set("pictureInPictureTabs", std::move(picture_in_picture))
      .Set("devToolsTabs", std::move(devtools))
      .Set("switcher", std::move(switcher))
      // Find in page's bar, the page shown's (the Swift's WindowState.find).
      .Set("find", FindState(root))
      // The page's menu shown last, while it may be chosen in; none of its
      // items while it comes or goes, nothing once it has gone; and how
      // many have shown.
      .Set("contextMenu",
           context_menus_->model()  ? base::Value(PageContextMenus::Describe(
                                          *context_menus_->model()))
           : context_menus_->held() ? base::Value(base::ListValue())
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
