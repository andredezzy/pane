// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/automation.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path_watcher.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "pane/app/data_launch.h"
#include "pane/automation/input.h"
#include "pane/automation/windows.h"
#include "pane/browser/profile_cache.h"
#include "pane/model/app_settings.h"
#include "pane/model/model_store.h"
#include "pane/model/pin_entry.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/design/menu_button.h"
#include "pane/ui/panels/app_icon_picker.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/render.h"
#include "pane/ui/sidebar/profile_row.h"
#include "pane/ui/sidebar/sidebar_view.h"
#include "pane/ui/sidebar/tab_row.h"
#include "pane/ui/tabs/tab_switcher_view.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "pane/ui/window/window.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/compositor/compositor.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace pane {

namespace {

// A view's frame in the window, from its top left: x, y, width, height.
base::ListValue FrameIn(const views::View* view, const views::View* root) {
  const gfx::Rect frame =
      views::View::ConvertRectToTarget(view, root, view->GetLocalBounds());
  return base::ListValue()
      .Append(frame.x())
      .Append(frame.y())
      .Append(frame.width())
      .Append(frame.height());
}

// A row's menu as it shows: each item's title, whether it's enabled, checked,
// and has its icon; a separator as one titled "-".
base::ListValue MenuItems(const ui::SimpleMenuModel& menu) {
  base::ListValue items;
  for (size_t index = 0; index < menu.GetItemCount(); ++index) {
    if (menu.GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR) {
      items.Append(base::DictValue().Set("title", "-"));
      continue;
    }
    items.Append(base::DictValue()
                     .Set("title", menu.GetLabelAt(index))
                     .Set("enabled", menu.IsEnabledAt(index))
                     .Set("checked", menu.IsItemCheckedAt(index))
                     .Set("icon", !menu.GetIconAt(index).IsEmpty()));
  }
  return items;
}

// One phase of a drag at a point of the window (from its top left): the
// press, a move, or the release, the left button's, to the window's root
// view as Views' own mouse event, which hit-tests it and hands it to what is
// under the press, as a drag with the trackpad reaches it. Not through the
// widget: it takes the moves to the pressed view only while a real button is
// down (Widget::OnMouseEvent, the Mac's [NSEvent pressedMouseButtons]),
// which no posted event sets, and a press it takes while the user's own
// button happens to be down captures the mouse, and with it the user's
// release elsewhere. The pointer moves to the press through the widget
// first, as a hand's gets there before it presses, so the widget knows
// where it is and what is under it hears when it leaves. Timed from its
// dispatch to the window's layout at it, the work before the next frame:
// the time, in milliseconds.
double Pointer(views::Widget* widget,
               ui::EventType type,
               const gfx::PointF& at) {
  views::View* root = widget->GetRootView();
  if (type == ui::EventType::kMousePressed) {
    ui::MouseEvent move(ui::EventType::kMouseMoved, at, at,
                        ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);
    widget->OnMouseEvent(&move);
  }
  const ui::MouseEvent event(type, at, at, ui::EventTimeForNow(),
                             ui::EF_LEFT_MOUSE_BUTTON,
                             ui::EF_LEFT_MOUSE_BUTTON);
  const base::TimeTicks start = base::TimeTicks::Now();
  if (type == ui::EventType::kMousePressed) {
    root->OnMousePressed(event);
  } else if (type == ui::EventType::kMouseDragged) {
    root->OnMouseDragged(event);
  } else {
    root->OnMouseReleased(event);
  }
  widget->LayoutRootViewIfNecessary();
  return (base::TimeTicks::Now() - start).InMillisecondsF();
}

// A drag from one point of the window to another (from its top left), in
// `steps` moves, each a Pointer phase: the moves' times, in milliseconds.
std::vector<double> Drag(views::Widget* widget,
                         const gfx::Point& from,
                         const gfx::Point& to,
                         int steps) {
  std::vector<double> times;
  Pointer(widget, ui::EventType::kMousePressed, gfx::PointF(from));
  steps = std::max(steps, 1);
  for (int step = 1; step <= steps; ++step) {
    const double share = static_cast<double>(step) / steps;
    const gfx::PointF at(std::lround(from.x() + (to.x() - from.x()) * share),
                         std::lround(from.y() + (to.y() - from.y()) * share));
    times.push_back(Pointer(widget, ui::EventType::kMouseDragged, at));
  }
  Pointer(widget, ui::EventType::kMouseReleased, gfx::PointF(to));
  return times;
}

// A cookie without its value: where it applies, and its attributes, as the
// Swift app's channel described one.
base::DictValue DescribeCookie(const net::CanonicalCookie& cookie) {
  std::string_view same_site = "unspecified";
  switch (cookie.SameSite()) {
    case net::CookieSameSite::NO_RESTRICTION:
      same_site = "none";
      break;
    case net::CookieSameSite::LAX_MODE:
      same_site = "lax";
      break;
    case net::CookieSameSite::STRICT_MODE:
      same_site = "strict";
      break;
    case net::CookieSameSite::UNSPECIFIED:
      break;
  }
  return base::DictValue()
      .Set("name", cookie.Name())
      .Set("domain", cookie.Domain())
      .Set("path", cookie.Path())
      .Set("secure", cookie.SecureAttribute())
      .Set("httpOnly", cookie.IsHttpOnly())
      .Set("sameSite", same_site)
      .Set("expires",
           cookie.IsPersistent()
               ? base::Value(cookie.ExpiryDate().InSecondsFSinceUnixEpoch())
               : base::Value());
}

// The key a scenario names, as PostKey takes it ("\x1b" for Esc, "\r" for
// Return, "\t" for Tab, " ", a letter or a digit), as Views codes it.
std::optional<ui::KeyboardCode> KeyCodeOf(std::string_view key) {
  if (key == "\x1b") {
    return ui::VKEY_ESCAPE;
  }
  if (key == "\r") {
    return ui::VKEY_RETURN;
  }
  if (key == "\t") {
    return ui::VKEY_TAB;
  }
  if (key == " ") {
    return ui::VKEY_SPACE;
  }
  if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
    return static_cast<ui::KeyboardCode>(ui::VKEY_A + (key[0] - 'a'));
  }
  if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
    return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (key[0] - '0'));
  }
  return std::nullopt;
}

int FlagsOf(const base::ListValue* modifiers) {
  int flags = ui::EF_NONE;
  if (!modifiers) {
    return flags;
  }
  for (const base::Value& modifier : *modifiers) {
    const std::string* name = modifier.GetIfString();
    if (!name) {
      continue;
    }
    if (*name == "command") {
      flags |= ui::EF_COMMAND_DOWN;
    } else if (*name == "shift") {
      flags |= ui::EF_SHIFT_DOWN;
    } else if (*name == "control") {
      flags |= ui::EF_CONTROL_DOWN;
    } else if (*name == "option") {
      flags |= ui::EF_ALT_DOWN;
    }
  }
  return flags;
}

std::optional<model::BrowserModel::ProxyStatus> ProxyStatusNamed(
    std::string_view name) {
  if (name == "testing") {
    return model::BrowserModel::ProxyStatus::kTesting;
  }
  if (name == "connected") {
    return model::BrowserModel::ProxyStatus::kConnected;
  }
  if (name == "failed") {
    return model::BrowserModel::ProxyStatus::kFailed;
  }
  return std::nullopt;
}

}  // namespace

// The channel's files, on a sequence that may block: commands as they
// arrive, read, removed, and handed to the UI; replies written whole, under
// a temporary name first, as the runner reads a file the moment it shows.
class Automation::Files {
 public:
  using Received =
      base::RepeatingCallback<void(std::string name, base::DictValue command)>;

  Files(const base::FilePath& directory, Received received)
      : in_(directory.AppendASCII("in")),
        out_(directory.AppendASCII("out")),
        received_(std::move(received)) {
    base::CreateDirectory(in_);
    base::CreateDirectory(out_);
    watcher_.Watch(
        in_, base::FilePathWatcher::Type::kNonRecursive,
        base::BindRepeating(&Files::Changed, base::Unretained(this)));
    // What came before the watch began.
    Changed(in_, false);
  }

  // A render the UI made, at the path a scenario named.
  void WriteRender(const base::FilePath& path, std::vector<uint8_t> png) {
    base::WriteFile(path, png);
  }

  void Write(const std::string& name, const std::string& json) {
    const base::FilePath reply =
        out_.Append(base::FilePath::FromUTF8Unsafe(name));
    const base::FilePath temporary = reply.AddExtensionASCII("tmp");
    if (base::WriteFile(temporary, json)) {
      base::ReplaceFile(temporary, reply, nullptr);
    }
  }

 private:
  void Changed(const base::FilePath& path, bool error) {
    std::vector<base::FilePath> files;
    base::FileEnumerator commands(in_, false, base::FileEnumerator::FILES,
                                  FILE_PATH_LITERAL("*.json"));
    for (base::FilePath file = commands.Next(); !file.empty();
         file = commands.Next()) {
      files.push_back(file);
    }
    // Their names sort in the order they were sent.
    std::ranges::sort(files);
    for (const base::FilePath& file : files) {
      std::string text;
      const bool read = base::ReadFileToString(file, &text);
      base::DeleteFile(file);
      std::optional<base::Value> command =
          read ? base::JSONReader::Read(text, base::JSON_PARSE_RFC)
               : std::nullopt;
      received_.Run(file.BaseName().AsUTF8Unsafe(),
                    command && command->is_dict()
                        ? std::move(*command).TakeDict()
                        : base::DictValue());
    }
  }

  const base::FilePath in_;
  const base::FilePath out_;
  Received received_;
  base::FilePathWatcher watcher_;
};

Automation::Automation(const base::FilePath& directory, UiApp* app)
    : app_(app) {
  files_ = base::SequenceBound<Files>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING}),
      directory,
      base::BindPostTaskToCurrentDefault(
          base::BindRepeating(&Automation::Run, weak_factory_.GetWeakPtr())));
}

Automation::~Automation() = default;

void Automation::Run(std::string name, base::DictValue command) {
  const std::string* command_name = command.FindString("command");
  Dispatch(command_name ? *command_name : std::string(), command,
           base::BindOnce(
               [](base::WeakPtr<Automation> automation, std::string name,
                  base::Value result, std::string error) {
                 if (!automation || !automation->app_) {
                   return;
                 }
                 base::DictValue reply;
                 reply.Set("ok", error.empty());
                 if (error.empty()) {
                   reply.Set("result", std::move(result));
                 } else {
                   reply.Set("error", error);
                 }
                 base::DictValue state = automation->app_->Snapshot();
                 automation->AddProfileReads(state);
                 state.Set("nativeWindows",
                           NativeWindows(automation->app_->window()->widget()));
                 state.Set("alert",
                           SheetAlert(automation->app_->window()->widget()));
                 reply.Set("state", std::move(state));
                 automation->files_.AsyncCall(&Files::Write)
                     .WithArgs(name, base::WriteJson(reply).value_or("{}"));
               },
               weak_factory_.GetWeakPtr(), std::move(name)));
}

void Automation::AddProfileReads(base::DictValue& state) const {
  base::ListValue* profiles = state.FindList("profiles");
  if (!profiles) {
    return;
  }
  for (base::Value& profile : *profiles) {
    base::DictValue& entry = profile.GetDict();
    const std::string* id = entry.FindString("id");
    if (!id) {
      continue;
    }
    const auto size = cache_sizes_.find(*id);
    entry.Set("cacheSize",
              size == cache_sizes_.end()
                  ? base::Value()
                  : base::Value(static_cast<double>(size->second)));
    const auto cookies = cookies_.find(*id);
    entry.Set("cookies", cookies == cookies_.end()
                             ? base::Value()
                             : base::Value(cookies->second.Clone()));
  }
}

void Automation::Dispatch(const std::string& name,
                          const base::DictValue& command,
                          Reply reply) {
  Window* window = app_->window();

  // An edit of the profiles before they load, or behind the lock, would edit
  // a model that holds none of them, and the load would take it back.
  if ((name == "createProfile" || name == "openTab" || name == "moveTab" ||
       name == "moveProfile" || name == "deleteProfile" ||
       name == "setKeepLoaded" || name == "setProxyStatus") &&
      !AppData::Get()->store().profiles_loaded()) {
    std::move(reply).Run(base::Value(), name + ": the profiles are not open");
    return;
  }

  if (name == "state") {
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  views::Widget* widget = window->widget();
  if (!widget &&
      (name == "render" || name == "key" || name == "clickWindow" ||
       name == "clickPage" || name == "hoverWindow" ||
       name == "holdModifiers" || name == "releaseModifiers" ||
       name == "hitTest" || name == "resizeWindow" || name == "pageLayout" ||
       name == "openProfileSheet" || name == "closeProfileSheet" ||
       name == "scrollWindow" || name == "dragWindow" ||
       name == "pointerWindow" || name == "frames" || name == "rowMenu" ||
       name == "clickModalButton" || name == "settingsMenu" ||
       name == "dispatchKey")) {
    std::move(reply).Run(base::Value(), name + ": the window is closed");
    return;
  }

  // What the address field's Return does with `input`: the selected page goes
  // there, or a new tab opens there (the Swift's navigate).
  if (name == "navigate") {
    const std::string* input = command.FindString("input");
    if (!input) {
      std::move(reply).Run(base::Value(), "navigate needs input");
      return;
    }
    app_->AddressEntered(base::UTF8ToUTF16(*input));
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The toolbar's Back, Forward, Reload (a crashed tab's too), and Stop, as
  // their presses run them.
  if (name == "back" || name == "forward" || name == "reload" ||
      name == "stop") {
    if (name == "back") {
      app_->Back();
    } else if (name == "forward") {
      app_->Forward();
    } else if (name == "reload") {
      app_->Reload();
    } else {
      app_->Stop();
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The update check (the app menu's, Settings'), the pill's click, its
  // dismiss, and Restart to update, which waits for a staged update (K1's
  // installer) and does nothing without one, as the Swift's did.
  if (name == "checkUpdates" || name == "downloadUpdate" ||
      name == "dismissUpdate" || name == "restartToUpdate") {
    if (name == "checkUpdates") {
      app_->updater().Check();
    } else if (name == "downloadUpdate") {
      app_->DownloadUpdate();
    } else if (name == "dismissUpdate") {
      app_->DismissUpdate();
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // What a click on the window does first: it becomes the key window, the app
  // in front, which a scenario's copy takes only when the runner allows it
  // (run.ts --foreground); and a window closed opens again, as the Dock's
  // click shows it.
  if (name == "activate") {
    app_->ShowWindow();
    window->Activate();
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The profile sheet's Create, as filled in: { name, color?, group?, spoof?,
  // savesHistory? }. Scenarios' profiles spoof nothing unless asked, so pages
  // see the engine's own values.
  if (name == "createProfile") {
    const std::string* profile_name = command.FindString("name");
    if (!profile_name) {
      std::move(reply).Run(base::Value(), "createProfile needs name");
      return;
    }
    model::ProfileDraft draft;
    draft.name = *profile_name;
    if (const std::string* color = command.FindString("color")) {
      draft.color =
          model::ColorNamed(*color).value_or(model::ProfileColor::kBlue);
    }
    if (const std::string* group = command.FindString("group")) {
      draft.group = *group;
    }
    draft.spoofing = command.FindBool("spoof").value_or(false);
    draft.saves_history = command.FindBool("savesHistory").value_or(true);
    if (draft.Validate()) {
      std::move(reply).Run(base::Value(), "createProfile needs a name");
      return;
    }
    std::move(reply).Run(base::Value(app_->CreateProfile(draft)),
                         std::string());
    return;
  }

  // A new tab, selected: { profile, url? }, at the start page without a URL.
  if (name == "openTab") {
    const std::string* profile_id = command.FindString("profile");
    if (!profile_id) {
      std::move(reply).Run(base::Value(), "openTab needs profile");
      return;
    }
    std::optional<GURL> url;
    if (const std::string* spec = command.FindString("url")) {
      if (GURL parsed(*spec); parsed.is_valid()) {
        url = std::move(parsed);
      }
    }
    std::move(reply).Run(base::Value(app_->OpenTab(*profile_id, url)),
                         std::string());
    return;
  }

  // A drag in the sidebar, as it lands: { profile, from, to } for a tab,
  // { from, to } for a profile.
  if (name == "moveTab" || name == "moveProfile") {
    const std::string* profile_id = command.FindString("profile");
    const std::optional<int> from = command.FindInt("from");
    const std::optional<int> to = command.FindInt("to");
    if (name == "moveTab" && !profile_id) {
      std::move(reply).Run(base::Value(), "moveTab needs profile");
      return;
    }
    if (!from || !to) {
      std::move(reply).Run(base::Value(),
                           name + (from ? " needs to" : " needs from"));
      return;
    }
    // The drop's intents, as a drag's lands.
    if (name == "moveTab") {
      app_->MoveTab(*profile_id, *from, *to);
    } else {
      app_->MoveProfile(*from, *to);
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The sidebar's header on an expanded profile: it collapses, its pages
  // unloading.
  if (name == "unloadProfile" || name == "deleteProfile") {
    const std::string* profile_id = command.FindString("profile");
    if (!profile_id) {
      std::move(reply).Run(base::Value(), name + " needs profile");
      return;
    }
    if (name == "unloadProfile") {
      app_->CollapseProfile(*profile_id);
    } else {
      // The alert's Delete, past the alert, which `rowMenu` and
      // `clickModalButton` press.
      app_->DeleteProfile(*profile_id);
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A profile's menu's Keep loaded: { profile, keepLoaded? }.
  if (name == "setKeepLoaded") {
    const std::string* profile_id = command.FindString("profile");
    if (!profile_id) {
      std::move(reply).Run(base::Value(), "setKeepLoaded needs profile");
      return;
    }
    app_->SetKeepLoaded(*profile_id,
                        command.FindBool("keepLoaded").value_or(true));
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A proxy test's answer, for the sidebar's dot: { profile, status:
  // "testing" | "connected" | "failed" | null }. It stands in for the test
  // itself until the proxy's port (N2) runs one.
  if (name == "setProxyStatus") {
    const std::string* profile_id = command.FindString("profile");
    const std::string* status = command.FindString("status");
    if (!profile_id) {
      std::move(reply).Run(base::Value(), "setProxyStatus needs profile");
      return;
    }
    const std::optional<model::BrowserModel::ProxyStatus> named =
        status ? ProxyStatusNamed(*status) : std::nullopt;
    if (status && !named) {
      std::move(reply).Run(base::Value(), "unknown proxy status");
      return;
    }
    app_->model().SetProxyStatus(*profile_id, named);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A tab's X, or its menu's Close tab: its page asks first (beforeunload).
  if (name == "closeTab") {
    const std::string* tab = command.FindString("tab");
    if (!tab) {
      std::move(reply).Run(base::Value(), "closeTab needs tab");
      return;
    }
    app_->CloseTab(*tab);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // { page: "browser" | "settings" }: the sidebar's Settings, or back.
  if (name == "showPage") {
    const std::string* page = command.FindString("page");
    if (!page || (*page != "browser" && *page != "settings")) {
      std::move(reply).Run(base::Value(), "unknown page");
      return;
    }
    app_->ShowPage(*page == "settings" ? model::BrowserModel::Page::kSettings
                                       : model::BrowserModel::Page::kBrowser);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  if (name == "select") {
    const std::string* tab = command.FindString("tab");
    if (!tab) {
      std::move(reply).Run(base::Value(), "select needs tab");
      return;
    }
    app_->Select(*tab);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The sad tab's Reload, for { tab }.
  if (name == "reloadCrashed") {
    const std::string* tab = command.FindString("tab");
    if (!tab) {
      std::move(reply).Run(base::Value(), "reloadCrashed needs tab");
      return;
    }
    app_->ReloadCrashed(*tab);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The tab switcher's keys, as the window's command table will run them
  // (H7): { action: "next" | "previous" } opens it or moves its selection,
  // "commit" is Ctrl's release, "cancel" is Esc.
  if (name == "switcher") {
    const std::string* action = command.FindString("action");
    if (!action || (*action != "next" && *action != "previous" &&
                    *action != "commit" && *action != "cancel")) {
      std::move(reply).Run(base::Value(), "unknown switcher action");
      return;
    }
    if (*action == "next" || *action == "previous") {
      app_->SwitchTab(/*backward=*/*action == "previous");
    } else if (*action == "commit") {
      app_->CommitSwitcher();
    } else {
      app_->CancelSwitcher();
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // Settings' Sleep: { tabSleepAfterMinutes, profileUnloadAfterMinutes,
  // cacheBudgetMB }, each a number, or null for off.
  if (name == "setSleepSettings") {
    app_->ChangeSettings([&](model::AppSettings& settings) {
      settings.tab_sleep_after_minutes =
          command.FindInt("tabSleepAfterMinutes");
      settings.profile_unload_after_minutes =
          command.FindInt("profileUnloadAfterMinutes");
      settings.cache_budget_mb = command.FindInt("cacheBudgetMB");
    });
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // { profile }: an open profile's HTTP cache's bytes, or its cookies, read
  // in the background; the replies' profiles[].cacheSize and
  // profiles[].cookies hold them once read.
  if (name == "readCacheSize" || name == "readCookies") {
    const std::string* profile_id = command.FindString("profile");
    Profile* profile = profile_id ? app_->ProfileIfOpen(*profile_id) : nullptr;
    if (!profile) {
      std::move(reply).Run(
          base::Value(), profile_id ? "profile " + *profile_id + " is not open"
                                    : name + " needs profile");
      return;
    }
    if (name == "readCacheSize") {
      cache_sizes_.erase(*profile_id);
      ComputeCacheSize(profile, base::BindOnce(
                                    [](base::WeakPtr<Automation> automation,
                                       std::string profile_id, int64_t bytes) {
                                      if (automation) {
                                        automation->cache_sizes_[profile_id] =
                                            bytes;
                                      }
                                    },
                                    weak_factory_.GetWeakPtr(), *profile_id));
    } else {
      cookies_.erase(*profile_id);
      profile->GetDefaultStoragePartition()
          ->GetCookieManagerForBrowserProcess()
          ->GetAllCookies(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(
                  [](base::WeakPtr<Automation> automation,
                     std::string profile_id, const net::CookieList& cookies) {
                    if (!automation) {
                      return;
                    }
                    base::ListValue described;
                    for (const net::CanonicalCookie& cookie : cookies) {
                      described.Append(DescribeCookie(cookie));
                    }
                    automation->cookies_[profile_id] = std::move(described);
                  },
                  weak_factory_.GetWeakPtr(), *profile_id),
              net::CookieList()));
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // { profile, path }: where an open profile's downloads go, as its download
  // preferences hold it.
  if (name == "setDownloadDirectory") {
    const std::string* profile_id = command.FindString("profile");
    const std::string* path = command.FindString("path");
    if (!profile_id || !path) {
      std::move(reply).Run(base::Value(),
                           "setDownloadDirectory needs profile and path");
      return;
    }
    Profile* profile = app_->ProfileIfOpen(*profile_id);
    if (!profile) {
      std::move(reply).Run(base::Value(),
                           "profile " + *profile_id + " is not open");
      return;
    }
    DownloadPrefs::FromBrowserContext(profile)->SetDownloadPath(
        base::FilePath::FromUTF8Unsafe(*path));
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // What changed, written: the reply comes once it is on the disk.
  if (name == "save") {
    app_->SaveNow(base::BindOnce(
        [](Reply reply) { std::move(reply).Run(base::Value(), std::string()); },
        std::move(reply)));
    return;
  }

  if (name == "theme") {
    const std::string* theme_name = command.FindString("theme");
    if (!theme_name) {
      std::move(reply).Run(base::Value(), "theme needs theme");
      return;
    }
    const std::optional<model::Theme> theme = model::ThemeNamed(*theme_name);
    if (!theme) {
      std::move(reply).Run(base::Value(), "unknown theme");
      return;
    }
    app_->ChangeSettings(
        [&](model::AppSettings& settings) { settings.theme = *theme; });
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // { appIcon: "automatic" | "day" | … }: the App icon, as the picker's sky
  // sets it, at any time, as the theme's.
  if (name == "appIcon") {
    const std::string* icon_name = command.FindString("appIcon");
    std::optional<model::AppIcon> icon;
    for (const model::AppIcon each : AllAppIcons()) {
      if (icon_name && model::AppIconName(each) == *icon_name) {
        icon = each;
      }
    }
    if (!icon) {
      std::move(reply).Run(base::Value(), "unknown app icon");
      return;
    }
    app_->ChangeSettings(
        [&](model::AppSettings& settings) { settings.app_icon = *icon; });
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // { open }: Settings' App icon, as its press opens the picker, or the
  // picker's Done.
  if (name == "appIconPicker") {
    if (command.FindBool("open").value_or(true)) {
      app_->OpenAppIconPicker();
    } else {
      app_->CloseAppIconPicker();
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // Settings' Set up, Change, or Remove PIN.
  if (name == "showPinScreen") {
    const std::string* mode = command.FindString("mode");
    if (!mode) {
      std::move(reply).Run(base::Value(), "showPinScreen needs mode");
      return;
    }
    AppData* data = AppData::Get();
    if (*mode == "setup") {
      data->ShowPinScreen(model::PinEntry::Mode::kSetup);
    } else if (*mode == "change") {
      data->ShowPinScreen(model::PinEntry::Mode::kChange);
    } else if (*mode == "remove") {
      data->ShowPinScreen(model::PinEntry::Mode::kRemove);
    } else {
      std::move(reply).Run(base::Value(), "unknown PIN screen mode");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // { keys: "1234" }: the numpad's digits; "<" is Delete, ">" is Continue,
  // "^" is the back arrow.
  if (name == "pinKeys") {
    const std::string* keys = command.FindString("keys");
    if (!keys) {
      std::move(reply).Run(base::Value(), "pinKeys needs keys");
      return;
    }
    AppData* data = AppData::Get();
    for (const char key : *keys) {
      switch (key) {
        case '<':
          data->PinBackspace();
          break;
        case '>':
          data->ContinueWithPin();
          break;
        case '^':
          data->DismissPinScreen();
          break;
        default:
          data->PinDigit(static_cast<char16_t>(key));
          break;
      }
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // Lock Pane (⌃⌘L, Paper 07.9).
  if (name == "lockNow") {
    app_->LockNow();
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The menu bar's items, by path, as the Swift app's menu command reads them.
  if (name == "menu") {
    const base::ListValue* path = command.FindList("path");
    std::vector<std::string> items;
    if (path) {
      for (const base::Value& item : *path) {
        items.push_back(item.is_string() ? item.GetString() : std::string());
      }
    }
    // Whether it ran, as a click on it in the menu bar: behind the Lock,
    // only Check for Updates… does (the Swift's menu validation).
    bool ran = app_->InteractionAllowed();
    if (items == std::vector<std::string>{"File", "New Tab"}) {
      app_->NewTab();
    } else if (items ==
               std::vector<std::string>{"Navigate", "Focus Address Bar"}) {
      app_->FocusAddressField();
    } else if (items ==
               std::vector<std::string>{"Pane", "Check for Updates…"}) {
      app_->updater().Check();
      ran = true;
    } else {
      std::move(reply).Run(base::Value(), "no such menu item");
      return;
    }
    std::move(reply).Run(base::Value(ran), std::string());
    return;
  }

  // The window drawn to a PNG, the page left out (pane/ui/render.h), at the
  // screen's scale.
  if (name == "render") {
    const std::string* path = command.FindString("path");
    std::optional<std::vector<uint8_t>> png =
        path ? RenderView(widget->GetRootView(),
                          widget->GetCompositor()
                              ? widget->GetCompositor()->device_scale_factor()
                              : 2)
             : std::nullopt;
    if (!png) {
      std::move(reply).Run(base::Value(), "cannot render");
      return;
    }
    // Written before the reply, so the file is there when the runner reads
    // the reply.
    files_.AsyncCall(&Files::WriteRender)
        .WithArgs(base::FilePath::FromUTF8Unsafe(*path), std::move(*png))
        .Then(base::BindOnce(
            [](Reply reply) {
              std::move(reply).Run(base::Value(), std::string());
            },
            std::move(reply)));
    return;
  }

  // Real keys, through the window's event path (pane/automation/input.h).
  if (name == "key") {
    const std::string* key = command.FindString("key");
    std::vector<std::string> modifiers;
    if (const base::ListValue* names = command.FindList("modifiers")) {
      for (const base::Value& modifier : *names) {
        if (modifier.is_string()) {
          modifiers.push_back(modifier.GetString());
        }
      }
    }
    if (!key || !PostKey(widget, *key, modifiers)) {
      std::move(reply).Run(base::Value(), "no key code for that key");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A real click at a point of the window (from its top left).
  if (name == "clickWindow") {
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    if (!x || !y) {
      std::move(reply).Run(base::Value(), "clickWindow needs x and y");
      return;
    }
    PostClick(widget, *x, *y, command.FindInt("clicks").value_or(1));
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A real click on the shown page, at a point of its view (from its top
  // left).
  if (name == "clickPage") {
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    if (!x || !y || !window->selected()) {
      std::move(reply).Run(base::Value(), !x || !y ? "clickPage needs x and y"
                                                   : "clickPage needs a page");
      return;
    }
    const gfx::Rect page = window->PageBounds();
    PostClick(widget, page.x() + *x, page.y() + *y, 1);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The pointer over a point of the window (from its top left), as a mouse
  // move the window dispatches; the real pointer stays where it is.
  if (name == "hoverWindow") {
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    if (!x || !y) {
      std::move(reply).Run(base::Value(), "hoverWindow needs x and y");
      return;
    }
    PostMove(widget, *x, *y);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  if (name == "releaseModifiers" || name == "holdModifiers") {
    std::vector<std::string> modifiers;
    if (const base::ListValue* names = command.FindList("modifiers");
        names && name == "holdModifiers") {
      for (const base::Value& modifier : *names) {
        if (modifier.is_string()) {
          modifiers.push_back(modifier.GetString());
        }
      }
    }
    PostModifiers(widget, modifiers);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // What a press at a point of the window (from its top left) would reach:
  // the view Views would hand it, and whether the page or Views takes it
  // (frame.h). A question, not input: it runs without the front.
  if (name == "hitTest") {
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    if (!x || !y) {
      std::move(reply).Run(base::Value(), "hitTest needs x and y");
      return;
    }
    const gfx::Point point(*x, *y);
    const views::View* handler =
        widget->GetRootView()->GetEventHandlerForPoint(point);
    const PressTarget target = PressTargetAt(widget, point);
    // The platform's own search for who takes a press there, as each mouse
    // event runs it: the median of a hundred and one, in microseconds.
    std::vector<base::TimeDelta> times;
    for (int i = 0; i < 101; ++i) {
      const base::TimeTicks start = base::TimeTicks::Now();
      PressTargetAt(widget, point);
      times.push_back(base::TimeTicks::Now() - start);
    }
    std::nth_element(times.begin(), times.begin() + 50, times.end());
    std::move(reply).Run(
        base::Value(base::DictValue()
                        .Set("handler",
                             handler ? handler->GetClassName() : std::string())
                        // The view by its id, or the nearest one around it:
                        // what a scenario names.
                        .Set("view", std::string(NearestViewIdName(handler)))
                        .Set("platform", PlatformViewAt(widget, point))
                        .Set("target", target == PressTarget::kPage    ? "page"
                                       : target == PressTarget::kViews ? "views"
                                       : target == PressTarget::kDrag ? "drag"
                                                                      : "other")
                        .Set("micros", times[50].InMicrosecondsF())),
        std::string());
    return;
  }

  // A trackpad scroll at a point of the window (from its top left), by `dy`
  // points: negative moves the content up, as a swipe up does.
  if (name == "scrollWindow") {
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    const std::optional<double> dy = command.FindDouble("dy");
    if (!x || !y || !dy) {
      std::move(reply).Run(base::Value(), "scrollWindow needs x, y, and dy");
      return;
    }
    PostScroll(widget, *x, *y, *dy);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A drag from one point of the window to another, { from: [x, y], to:
  // [x, y], steps }, through Views' own dispatch (Drag): each step timed from
  // its event to the window's layout, and the page's width after it.
  if (name == "dragWindow") {
    const base::ListValue* from = command.FindList("from");
    const base::ListValue* to = command.FindList("to");
    const auto point =
        [](const base::ListValue* pair) -> std::optional<gfx::Point> {
      const std::optional<double> x =
          pair && pair->size() == 2 ? (*pair)[0].GetIfDouble() : std::nullopt;
      const std::optional<double> y =
          pair && pair->size() == 2 ? (*pair)[1].GetIfDouble() : std::nullopt;
      if (!x || !y) {
        return std::nullopt;
      }
      return gfx::Point(*x, *y);
    };
    const std::optional<gfx::Point> start = point(from);
    const std::optional<gfx::Point> end = point(to);
    if (!start || !end) {
      std::move(reply).Run(base::Value(), "dragWindow needs from and to");
      return;
    }
    base::ListValue steps;
    for (double ms :
         Drag(widget, *start, *end, command.FindInt("steps").value_or(10))) {
      steps.Append(ms);
    }
    std::move(reply).Run(
        base::Value(base::DictValue()
                        .Set("stepMs", std::move(steps))
                        .Set("pageWidth", window->PageBounds().width())),
        std::string());
    return;
  }

  // One phase of a drag at a point of the window (from its top left),
  // { phase: "down" | "drag" | "up", x, y }, through Views' own dispatch
  // (Pointer), so a scenario paces a drag as a hand does and reads the rows
  // between its moves, in the background too: the step's time to the
  // window's layout, in milliseconds.
  if (name == "pointerWindow") {
    const std::string* phase = command.FindString("phase");
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    const std::optional<ui::EventType> type =
        !phase             ? std::nullopt
        : *phase == "down" ? std::optional(ui::EventType::kMousePressed)
        : *phase == "drag" ? std::optional(ui::EventType::kMouseDragged)
        : *phase == "up"   ? std::optional(ui::EventType::kMouseReleased)
                           : std::nullopt;
    if (!type || !x || !y) {
      std::move(reply).Run(base::Value(),
                           !type ? "pointerWindow's phase is down, drag, or up"
                                 : "pointerWindow needs x and y");
      return;
    }
    const double ms = Pointer(widget, *type, gfx::PointF(*x, *y));
    std::move(reply).Run(base::Value(base::DictValue().Set("ms", ms)),
                         std::string());
    return;
  }

  // Where the sidebar's and the toolbar's parts are, each shown one by what
  // the Swift app named it ("pane.sidebar.profiles", "pane.tab.<id>",
  // "pane.toolbar.back"), from the window's top left: x, y, width, height. A
  // row scrolled out of view lies outside the list's.
  if (name == "frames") {
    const std::string* prefix = command.FindString("prefix");
    base::DictValue frames;
    const views::View* root = widget->GetRootView();
    std::vector<std::pair<std::string, const views::View*>> named =
        window->root()->sidebar()->NamedViews();
    for (auto& entry : window->root()->toolbar()->NamedViews()) {
      named.push_back(std::move(entry));
    }
    for (const int id : {kViewIdSadTab, kViewIdSadTabReload}) {
      if (const views::View* view = window->root()->GetViewByID(id)) {
        named.emplace_back(std::string(ViewIdName(id)), view);
      }
    }
    for (auto& entry : window->root()->settings()->NamedViews()) {
      named.push_back(std::move(entry));
    }
    if (AppIconPicker* picker = window->root()->app_icon_picker()) {
      for (auto& entry : picker->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    for (const auto& [view_name, view] : named) {
      if (view->IsDrawn() && (!prefix || view_name.starts_with(*prefix))) {
        frames.Set(view_name, FrameIn(view, root));
      }
    }
    // The switcher's panel and rows, which it draws itself.
    if (TabSwitcherView* switcher = window->root()->switcher();
        switcher && switcher->IsDrawn()) {
      for (const auto& [row_name, row] : switcher->NamedFrames()) {
        if (!prefix || row_name.starts_with(*prefix)) {
          const gfx::Rect frame =
              views::View::ConvertRectToTarget(switcher, root, row);
          frames.Set(row_name, base::ListValue()
                                   .Append(frame.x())
                                   .Append(frame.y())
                                   .Append(frame.width())
                                   .Append(frame.height()));
        }
      }
    }
    std::move(reply).Run(base::Value(std::move(frames)), std::string());
    return;
  }

  // A menu of Settings' (its name, "pane.settings.theme"), as its model holds
  // what the Mac's menu shows: its items; with `choose`, the item of that
  // title chosen, as a click in the menu chooses it. The menu itself, which
  // runs with the app's tasks held, is the front's.
  if (name == "settingsMenu") {
    const std::string* menu_name = command.FindString("menu");
    SettingsView* settings = window->root()->settings();
    MenuButton* menu = menu_name && settings->IsDrawn()
                           ? settings->FindMenu(*menu_name)
                           : nullptr;
    if (!menu || !menu->IsDrawn()) {
      std::move(reply).Run(base::Value(),
                           "settingsMenu needs a menu Settings shows");
      return;
    }
    ui::SimpleMenuModel& model = menu->menu_model();
    base::ListValue items = MenuItems(model);
    if (const std::string* choose = command.FindString("choose")) {
      std::optional<size_t> chosen;
      for (size_t index = 0; index < model.GetItemCount(); ++index) {
        if (base::UTF16ToUTF8(model.GetLabelAt(index)) == *choose) {
          chosen = index;
        }
      }
      if (!chosen) {
        std::move(reply).Run(base::Value(), "no menu item " + *choose);
        return;
      }
      model.ActivatedAt(*chosen);
    }
    std::move(reply).Run(base::Value(std::move(items)), std::string());
    return;
  }

  // A key through Views' own dispatch, { key, modifiers }, as the platform
  // hands the window a key: the window's accelerators, then the focused
  // view, in the background too, where `key` (AppKit's path) needs the
  // front.
  if (name == "dispatchKey") {
    const std::string* key = command.FindString("key");
    const std::optional<ui::KeyboardCode> code =
        key ? KeyCodeOf(*key) : std::nullopt;
    if (!code) {
      std::move(reply).Run(base::Value(), "no key code for that key");
      return;
    }
    const int flags = FlagsOf(command.FindList("modifiers"));
    for (const ui::EventType type :
         {ui::EventType::kKeyPressed, ui::EventType::kKeyReleased}) {
      ui::KeyEvent event(type, *code, flags);
      widget->OnKeyEvent(&event);
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A row's right-click menu, as its model holds what the Mac's menu shows:
  // { profile } or { tab }, its items; with `choose`, the item of that title
  // chosen, as a click in the menu chooses it, after the menu has gone. The
  // menu itself, which runs with the app's tasks held, is the front's.
  if (name == "rowMenu") {
    const std::string* profile_id = command.FindString("profile");
    const std::string* tab_id = command.FindString("tab");
    SidebarView* sidebar = window->root()->sidebar();
    ProfileRow* profile_row =
        profile_id ? sidebar->FindProfileRow(*profile_id) : nullptr;
    TabRow* tab_row = tab_id ? sidebar->FindTabRow(*tab_id) : nullptr;
    ui::SimpleMenuModel* menu = profile_row ? &profile_row->menu_model()
                                : tab_row   ? &tab_row->menu_model()
                                            : nullptr;
    if (!menu) {
      std::move(reply).Run(base::Value(),
                           "rowMenu needs a row the sidebar shows");
      return;
    }
    base::ListValue items = MenuItems(*menu);
    if (const std::string* choose = command.FindString("choose")) {
      std::optional<size_t> chosen;
      for (size_t index = 0; index < menu->GetItemCount(); ++index) {
        if (menu->GetTypeAt(index) != ui::MenuModel::TYPE_SEPARATOR &&
            base::UTF16ToUTF8(menu->GetLabelAt(index)) == *choose) {
          chosen = index;
        }
      }
      if (!chosen || !menu->IsEnabledAt(*chosen)) {
        std::move(reply).Run(base::Value(), "no menu item " + *choose);
        return;
      }
      menu->ActivatedAt(*chosen);
    }
    std::move(reply).Run(base::Value(std::move(items)), std::string());
    return;
  }

  // A button of the alert the window shows as a sheet, { title }, pressed as
  // a click on it does.
  if (name == "clickModalButton") {
    const std::string* title = command.FindString("title");
    if (!title || !PressSheetButton(widget, *title)) {
      std::move(reply).Run(base::Value(), "no button " +
                                              (title ? *title : std::string()) +
                                              " in the window's sheet");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The window at a new size, as one step of a live resize sets it: how long
  // the step took, in milliseconds, and where the page went.
  if (name == "resizeWindow") {
    const std::optional<int> width = command.FindInt("width");
    const std::optional<int> height = command.FindInt("height");
    if (!width || !height) {
      std::move(reply).Run(base::Value(),
                           "resizeWindow needs width and height");
      return;
    }
    gfx::Rect bounds = widget->GetWindowBoundsInScreen();
    bounds.set_size(gfx::Size(*width, *height));
    const base::TimeTicks start = base::TimeTicks::Now();
    widget->SetBounds(bounds);
    const base::TimeDelta took = base::TimeTicks::Now() - start;
    const gfx::Rect page = window->PageBounds();
    const gfx::Rect client = widget->GetClientAreaBoundsInScreen();
    std::move(reply).Run(
        base::Value(base::DictValue()
                        .Set("ms", took.InMillisecondsF())
                        .Set("window", base::ListValue()
                                           .Append(client.width())
                                           .Append(client.height()))
                        .Set("page", base::ListValue()
                                         .Append(page.x())
                                         .Append(page.y())
                                         .Append(page.width())
                                         .Append(page.height()))),
        std::string());
    return;
  }

  // Where the page's card is, in the window's points from its top left, and
  // the corners its layer rounds (top left, top right, bottom right, bottom
  // left), which a render leaves out with the page.
  if (name == "pageLayout") {
    const gfx::Rect page = window->PageBounds();
    const gfx::RoundedCornersF corners = window->root()->PageCorners();
    std::move(reply).Run(
        base::Value(base::DictValue()
                        .Set("page", base::ListValue()
                                         .Append(page.x())
                                         .Append(page.y())
                                         .Append(page.width())
                                         .Append(page.height()))
                        .Set("corners", base::ListValue()
                                            .Append(corners.upper_left())
                                            .Append(corners.upper_right())
                                            .Append(corners.lower_right())
                                            .Append(corners.lower_left()))),
        std::string());
    return;
  }

  // The prototype's sheet stands in for the profile sheet (G4): the
  // footer's New opens it.
  if (name == "openProfileSheet" || name == "closeProfileSheet") {
    if (name == "openProfileSheet") {
      app_->NewProfile();
    } else {
      window->root()->ShowSheet(false);
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // Saved, then closed, then the volume detached: the reply comes once the
  // save is on the disk, as the Swift app's did.
  if (name == "quit") {
    app_->Quit(base::BindOnce(
        [](Reply reply) { std::move(reply).Run(base::Value(), std::string()); },
        std::move(reply)));
    return;
  }

  std::move(reply).Run(base::Value(), "unknown command " + name);
}

}  // namespace pane
