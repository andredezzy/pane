// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/automation.h"

#include <algorithm>
#include <cmath>
#include <map>
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
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "components/crx_file/id_util.h"
#include "components/tabs/public/tab_interface.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/uninstall_reason.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "pane/app/data_launch.h"
#include "pane/automation/file_panels.h"
#include "pane/automation/input.h"
#include "pane/automation/windows.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profile_cache.h"
#include "pane/model/app_settings.h"
#include "pane/model/model_store.h"
#include "pane/model/pin_entry.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/ui/app/menu_bar.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/menu_button.h"
#include "pane/ui/dialogs/extension_prompt_view.h"
#include "pane/ui/dialogs/page_dialog.h"
#include "pane/ui/extensions/action_menu.h"
#include "pane/ui/extensions/action_popup.h"
#include "pane/ui/extensions/extension_actions_view.h"
#include "pane/ui/extensions/extensions_menu_view.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/page/find_bar_view.h"
#include "pane/ui/page/page_context_menu.h"
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
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/compositor.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield.h"
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
  if (key == "control") {
    return ui::VKEY_CONTROL;
  }
  if (key == "shift") {
    return ui::VKEY_SHIFT;
  }
  if (key == ",") {
    return ui::VKEY_OEM_COMMA;
  }
  if (key == "[") {
    return ui::VKEY_OEM_4;
  }
  if (key == "]") {
    return ui::VKEY_OEM_6;
  }
  if (key == "=") {
    return ui::VKEY_OEM_PLUS;
  }
  if (key == "-") {
    return ui::VKEY_OEM_MINUS;
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

namespace {

// A command's { modifiers }, by the names PostKey takes.
std::vector<std::string> Modifiers(const base::DictValue& command) {
  std::vector<std::string> modifiers;
  if (const base::ListValue* names = command.FindList("modifiers")) {
    for (const base::Value& modifier : *names) {
      if (modifier.is_string()) {
        modifiers.push_back(modifier.GetString());
      }
    }
  }
  return modifiers;
}

}  // namespace

Automation::Automation(const base::FilePath& directory, UiApp* app)
    : app_(app) {
  // The system's file panels, as a scenario sees and answers them.
  WatchFilePanels();
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
                 // A file panel shows as the window's sheet, over any dialog;
                 // the system's print panel runs app-modal.
                 if (base::Value panel = FilePanelShown(); !panel.is_none()) {
                   state.Set("sheet", std::move(panel));
                 }
                 state.Set("modal", ModalPanel());
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
       name == "dispatchKey" || name == "clickSheetButton" ||
       name == "typeInPage" || name == "modalHost" ||
       name == "dialogAccessibility" || name == "fillSheet" ||
       name == "extensionsMenu" || name == "extensionsAccessibility")) {
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

  // Find in page, as the Swift's channel: { text, forward } shows the bar
  // (⌘F's own path) and finds `text` as typed into its field, or, the text it
  // holds already, steps to the next match, or the previous one (Return,
  // Shift-Return).
  if (name == "find") {
    const std::string* text = command.FindString("text");
    if (!text) {
      std::move(reply).Run(base::Value(), "find needs text");
      return;
    }
    RootView* root = window->root();
    const bool opens = root && !root->find_state().visible;
    if (opens) {
      app_->ShowFind();
    }
    if (!root || !root->find_bar()) {
      std::move(reply).Run(base::Value(), "find needs a page");
      return;
    }
    const std::u16string query = base::UTF8ToUTF16(*text);
    if (root->find_state().text == query) {
      app_->FindStep(command.FindBool("forward").value_or(true));
    } else {
      root->find_bar()->Type(query);
    }
    // The Swift's channel found, then showed the bar: a field taking the keys
    // then had all of its text selected.
    if (opens) {
      root->find_bar()->field()->SelectAll(false);
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The find bar's close.
  if (name == "closeFind") {
    app_->CloseFind();
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // What VoiceOver reads of find in page's bar while it shows (ADR 0071):
  // each part's role and name.
  if (name == "findAccessibility") {
    FindBarView* find = window->root() ? window->root()->find_bar() : nullptr;
    if (!find) {
      std::move(reply).Run(base::Value(), "no find bar");
      return;
    }
    base::DictValue parts;
    for (const auto& [part, view] : find->NamedViews()) {
      ui::AXNodeData data;
      view->GetViewAccessibility().GetAccessibleNodeData(&data);
      parts.Set(
          part,
          base::DictValue()
              .Set("role", ui::ToString(data.role))
              .Set("name",
                   data.GetStringAttribute(ax::mojom::StringAttribute::kName))
              .Set("ignored", view->GetViewAccessibility().GetIsIgnored()));
    }
    std::move(reply).Run(base::Value(std::move(parts)), std::string());
    return;
  }

  // What VoiceOver reads of the toolbar's extensions and the menus showing
  // (ADR 0071): each named view's role, name, and whether it is selected.
  if (name == "extensionsAccessibility") {
    std::vector<std::pair<std::string, const views::View*>> named;
    for (auto& entry :
         window->root()->toolbar()->extension_actions()->NamedViews()) {
      named.push_back(std::move(entry));
    }
    if (ExtensionsMenuView* menu = window->root()->extensions_menu()) {
      for (auto& entry : menu->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    if (ExtensionMoreMenuView* more = window->root()->extension_more_menu()) {
      for (auto& entry : more->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    base::DictValue parts;
    for (const auto& [part, view] : named) {
      if (!view->IsDrawn()) {
        continue;
      }
      ui::AXNodeData data;
      view->GetViewAccessibility().GetAccessibleNodeData(&data);
      parts.Set(
          part,
          base::DictValue()
              .Set("role", ui::ToString(data.role))
              .Set("name",
                   data.GetStringAttribute(ax::mojom::StringAttribute::kName))
              .Set("selected",
                   data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected))
              .Set("enabled",
                   data.GetRestriction() != ax::mojom::Restriction::kDisabled));
    }
    std::move(reply).Run(base::Value(std::move(parts)), std::string());
    return;
  }

  // The full screen's hint as Chrome's hook shows it (WindowHost's
  // ShowExclusiveAccessHint), { hint }, empty for none: its look and fade,
  // drawn without the front a page's fullscreen needs.
  if (name == "showExclusiveAccessHint") {
    const std::string* hint = command.FindString("hint");
    if (!hint) {
      std::move(reply).Run(base::Value(), "showExclusiveAccessHint needs hint");
      return;
    }
    window->ShowExclusiveAccessHint(base::UTF8ToUTF16(*hint));
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // View › Developer Tools (⇧⌘I).
  if (name == "toggleDevTools") {
    app_->ToggleDevTools();
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // An item of the page's menu shown last, by its title, as a click in the
  // menu chooses it: the platform's menu closes, and the menu is let go.
  if (name == "chooseContextMenuItem") {
    const std::string* title = command.FindString("title");
    if (!title || !app_->context_menus().Choose(base::UTF8ToUTF16(*title))) {
      std::move(reply).Run(base::Value(), "no context menu item " +
                                              (title ? *title : std::string()));
      return;
    }
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

  // An item of the menu bar chosen by its path, { path: ["File", "New
  // Tab"] }, as a click on it chooses it: through the platform's own menu
  // bar, its enabled states and titles brought up to date first, as when it
  // opens. Whether it ran: false where the menu disables it (behind the
  // Lock, all but Quit and Check for Updates…).
  if (name == "menu") {
    const base::ListValue* path = command.FindList("path");
    std::vector<std::u16string> items;
    if (path) {
      for (const base::Value& item : *path) {
        items.push_back(item.is_string() ? base::UTF8ToUTF16(item.GetString())
                                         : std::u16string());
      }
    }
    MenuBar::Native* menu_bar = app_->menu_bar().native();
    const std::optional<bool> ran =
        menu_bar ? menu_bar->Choose(items) : std::nullopt;
    if (!ran) {
      std::move(reply).Run(base::Value(), "no such menu item");
      return;
    }
    std::move(reply).Run(base::Value(*ran), std::string());
    return;
  }

  // The menu bar as it shows now: each menu's items, with their titles,
  // keys, and whether each is enabled, hidden, or checked.
  if (name == "menuBar") {
    MenuBar::Native* menu_bar = app_->menu_bar().native();
    std::move(reply).Run(
        base::Value(menu_bar ? menu_bar->Describe() : base::ListValue()),
        std::string());
    return;
  }

  // The window drawn to a PNG, the page left out (pane/ui/render.h), at the
  // screen's scale.
  if (name == "render") {
    const std::string* path = command.FindString("path");
    // An action's popup renders from its own window: { target: "popup" }.
    const std::string* target = command.FindString("target");
    views::Widget* drawn = widget;
    if (target && *target == "popup") {
      ActionPopup* popup = app_->Extensions().popup();
      drawn = popup && popup->shown() ? popup->widget() : nullptr;
    }
    std::optional<std::vector<uint8_t>> png =
        path && drawn
            ? RenderView(drawn->GetRootView(),
                         drawn->GetCompositor()
                             ? drawn->GetCompositor()->device_scale_factor()
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
    // Quit Pane?'s buttons, while it asks, and its card as "pane.quit", as
    // the Swift's dialog surface was named.
    if (window->root()->quit_dialog_showing()) {
      for (const int id : {kViewIdQuitConfirm, kViewIdQuitCancel}) {
        if (const views::View* view = window->root()->GetViewByID(id)) {
          named.emplace_back(std::string(ViewIdName(id)), view);
        }
      }
      const auto* dialog = static_cast<const ModalDialog*>(
          window->root()->GetViewByID(kViewIdQuit));
      const std::string_view card_name = ViewIdName(kViewIdQuit);
      if (dialog && (!prefix || card_name.starts_with(*prefix))) {
        const gfx::Rect card = views::View::ConvertRectToTarget(
            dialog, root, dialog->CardBounds());
        frames.Set(card_name, base::ListValue()
                                  .Append(card.x())
                                  .Append(card.y())
                                  .Append(card.width())
                                  .Append(card.height()));
      }
    }
    // A page's dialog: its card's content as "pane.dialog", its fields, and
    // its buttons.
    if (PageDialog* dialog = window->root()->page_dialog()) {
      for (auto& entry : dialog->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    // Find in page's bar, its field, count, and buttons, while it shows,
    // and its card as "pane.find", as the Swift named the bar.
    if (FindBarView* find = window->root()->find_bar()) {
      for (auto& entry : find->NamedViews()) {
        named.push_back(std::move(entry));
      }
      const std::string_view card_name = ViewIdName(kViewIdFind);
      if (!prefix || card_name.starts_with(*prefix)) {
        const gfx::Rect card = views::View::ConvertRectToTarget(
            window->root(), root, window->root()->FindBarBounds());
        frames.Set(card_name, base::ListValue()
                                  .Append(card.x())
                                  .Append(card.y())
                                  .Append(card.width())
                                  .Append(card.height()));
      }
    }
    // The extensions menu and the more menu, while they show.
    if (ExtensionsMenuView* menu = window->root()->extensions_menu()) {
      for (auto& entry : menu->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    if (ExtensionMoreMenuView* more = window->root()->extension_more_menu()) {
      for (auto& entry : more->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    // An extension's prompt, while it asks: its card by its own name, as
    // the Swift named the surface ("pane.extensionInstall",
    // "pane.extensionPopup"), its buttons, and Report abuse.
    if (ExtensionPromptView* prompt = window->root()->extension_prompt()) {
      for (auto& entry : prompt->NamedViews()) {
        if (entry.second != prompt) {
          named.push_back(std::move(entry));
          continue;
        }
        if (!prefix || entry.first.starts_with(*prefix)) {
          const gfx::Rect card = views::View::ConvertRectToTarget(
              window->root(), root, window->root()->ExtensionPromptBounds());
          frames.Set(entry.first, base::ListValue()
                                      .Append(card.x())
                                      .Append(card.y())
                                      .Append(card.width())
                                      .Append(card.height()));
        }
      }
    }
    // The page, DevTools docked beside it, and the full screen's hint.
    for (const int id :
         {kViewIdContent, kViewIdDevTools, kViewIdExclusiveAccessHint}) {
      if (const views::View* view = window->root()->GetViewByID(id)) {
        named.emplace_back(std::string(ViewIdName(id)), view);
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

  // A key through Views' own dispatch, { key, modifiers, phase }, as the
  // platform hands the window a key: the window's accelerators, then the
  // focused view, in the background too, where `key` (AppKit's path) needs
  // the front. `phase` is "press" or "release" for one half alone ("control"
  // let go, with no modifier left, is Ctrl's release); both by default.
  if (name == "dispatchKey") {
    const std::string* key = command.FindString("key");
    const std::optional<ui::KeyboardCode> code =
        key ? KeyCodeOf(*key) : std::nullopt;
    if (!code) {
      std::move(reply).Run(base::Value(), "no key code for that key");
      return;
    }
    const int flags = FlagsOf(command.FindList("modifiers"));
    const std::string* phase = command.FindString("phase");
    std::vector<ui::EventType> types;
    if (!phase || *phase != "release") {
      types.push_back(ui::EventType::kKeyPressed);
    }
    if (!phase || *phase != "press") {
      types.push_back(ui::EventType::kKeyReleased);
    }
    for (const ui::EventType type : types) {
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

  // A button of the page's dialog showing, by its title, { title, texts }:
  // its fields take `texts` first, in order, as a value set through
  // accessibility does, and the button takes a real click at its middle.
  if (name == "clickSheetButton") {
    const std::string* title = command.FindString("title");
    // An extension's prompt first, over a page's dialog: its accept or its
    // Cancel.
    if (const ExtensionPrompt* prompt = app_->Extensions().prompt();
        prompt && title) {
      ExtensionPromptView* view =
          window->root() ? window->root()->extension_prompt() : nullptr;
      const std::u16string asked = base::UTF8ToUTF16(*title);
      const views::View* button =
          !view                           ? nullptr
          : asked == prompt->accept_title ? view->accept_button()
          : asked == prompt->cancel_title ? view->cancel_button()
                                          : nullptr;
      if (!button) {
        std::move(reply).Run(base::Value(), "no button " + *title);
        return;
      }
      const gfx::Rect frame = views::View::ConvertRectToTarget(
          button, widget->GetRootView(), button->GetLocalBounds());
      PostClick(widget, frame.CenterPoint().x(), frame.CenterPoint().y(), 1);
      std::move(reply).Run(base::Value(), std::string());
      return;
    }
    PageDialog* dialog =
        window->root() ? window->root()->page_dialog() : nullptr;
    if (!title || !dialog) {
      std::move(reply).Run(base::Value(), "no dialog");
      return;
    }
    const std::vector<std::u16string>& buttons = dialog->content().buttons;
    const auto found = std::ranges::find(buttons, base::UTF8ToUTF16(*title));
    if (found == buttons.end()) {
      std::move(reply).Run(base::Value(), "no button " + *title);
      return;
    }
    if (const base::ListValue* texts = command.FindList("texts")) {
      std::vector<std::u16string> fields;
      for (const base::Value& text : *texts) {
        fields.push_back(text.is_string() ? base::UTF8ToUTF16(text.GetString())
                                          : std::u16string());
      }
      dialog->SetFieldTexts(fields);
    }
    const views::View* button = dialog->button(found - buttons.begin());
    const gfx::Rect frame = views::View::ConvertRectToTarget(
        button, widget->GetRootView(), button->GetLocalBounds());
    PostClick(widget, frame.CenterPoint().x(), frame.CenterPoint().y(), 1);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The page's dialog's fields given `texts`, in order, as a value set
  // through accessibility gives them (VoiceOver, Voice Control), with no
  // button pressed: { texts }.
  if (name == "fillSheet") {
    PageDialog* dialog =
        window->root() ? window->root()->page_dialog() : nullptr;
    const base::ListValue* texts = command.FindList("texts");
    if (!dialog || !texts) {
      std::move(reply).Run(base::Value(),
                           !dialog ? "no dialog" : "fillSheet needs texts");
      return;
    }
    std::vector<std::u16string> fields;
    for (const base::Value& text : *texts) {
      fields.push_back(text.is_string() ? base::UTF8ToUTF16(text.GetString())
                                        : std::u16string());
    }
    dialog->SetFieldTexts(fields);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // What assistive technologies read of the page's dialog showing (ADR
  // 0071), from Views' own accessibility data, as VoiceOver gets it: the
  // dialog's role, name, and modality, and each field's and button's name,
  // with the default button marked.
  if (name == "dialogAccessibility") {
    PageDialog* dialog =
        window->root() ? window->root()->page_dialog() : nullptr;
    if (!dialog) {
      std::move(reply).Run(base::Value(), "no dialog");
      return;
    }
    const auto describe = [](const views::View* view) {
      ui::AXNodeData data;
      view->GetViewAccessibility().GetAccessibleNodeData(&data);
      return base::DictValue()
          .Set("role", ui::ToString(data.role))
          .Set("name",
               data.GetStringAttribute(ax::mojom::StringAttribute::kName))
          .Set("modal", data.GetBoolAttribute(ax::mojom::BoolAttribute::kModal))
          .Set("default", data.HasState(ax::mojom::State::kDefault))
          .Set("protected", data.HasState(ax::mojom::State::kProtected));
    };
    base::ListValue fields;
    for (size_t index = 0; index < dialog->field_count(); ++index) {
      fields.Append(describe(dialog->field(index)));
    }
    base::ListValue buttons;
    for (size_t index = 0; index < dialog->button_count(); ++index) {
      buttons.Append(describe(dialog->button(index)));
    }
    std::move(reply).Run(base::Value(describe(dialog)
                                         .Set("fields", std::move(fields))
                                         .Set("buttons", std::move(buttons))),
                         std::string());
    return;
  }

  // The system's file panel showing answered as its own buttons would,
  // { paths } chosen or { cancel: true }, and taken down: a panel run out of
  // process takes no choice from here (file_panels.h).
  if (name == "answerFileChooser") {
    std::optional<std::vector<base::FilePath>> paths;
    if (!command.FindBool("cancel").value_or(false)) {
      paths.emplace();
      if (const base::ListValue* chosen = command.FindList("paths")) {
        for (const base::Value& path : *chosen) {
          if (path.is_string()) {
            paths->push_back(base::FilePath::FromUTF8Unsafe(path.GetString()));
          }
        }
      }
    }
    if (!AnswerFilePanel(std::move(paths))) {
      std::move(reply).Run(base::Value(), "no file chooser");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A key typed into the selected page, { key }: the page takes the
  // keyboard, and the window hands it the key as the user's (input a hung
  // page leaves unanswered), in the background too.
  if (name == "typeInPage") {
    const std::string* key = command.FindString("key");
    if (!window->selected() || !window->root()) {
      std::move(reply).Run(base::Value(), "no page");
      return;
    }
    window->root()->FocusPage();
    if (!key || !SendKeyToWindow(widget, *key, Modifiers(command))) {
      std::move(reply).Run(base::Value(), "no key code for that key");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A key, with { modifiers }, typed into whatever has the window's keys, as
  // the window hands it over, in the background too: the page, or a view of
  // Pane's (the address being edited), its shortcuts' paths and all.
  if (name == "windowKey") {
    const std::string* key = command.FindString("key");
    if (!key || !SendKeyToWindow(widget, *key, Modifiers(command))) {
      std::move(reply).Run(base::Value(), "no key code for that key");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // Installs from the Chrome Web Store by its id into a profile, opened in
  // the engine first if needed, as the store's own button does, Pane's
  // dialog asking first: { id, profile }. The reply comes once the install
  // has begun.
  if (name == "installExtension") {
    const std::string* id = command.FindString("id");
    const std::string* profile_id = command.FindString("profile");
    if (!id || !profile_id || !crx_file::id_util::IdIsValid(*id) ||
        !app_->model().FindProfile(*profile_id)) {
      std::move(reply).Run(
          base::Value(),
          "installExtension needs an extension's id and a profile");
      return;
    }
    PANE_TRACE("call", "PaneBrowserContext.installExtension",
               base::DictValue().Set("extension", *id));
    app_->WithProfile(
        *profile_id,
        base::BindOnce(
            [](std::string id, base::WeakPtr<views::Widget> window, Reply reply,
               Profile* profile) {
              if (!profile) {
                std::move(reply).Run(base::Value(),
                                     "installExtension: the profile would "
                                     "not open");
                return;
              }
              auto installer =
                  base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
                      id, profile,
                      window ? window->GetNativeWindow() : gfx::NativeWindow(),
                      base::BindOnce(
                          [](const std::string& id, bool success,
                             const std::string& error,
                             extensions::webstore_install::Result result) {
                            PANE_TRACE(
                                "callback",
                                "PaneBrowserContext.installExtensionDone",
                                base::DictValue()
                                    .Set("extension", id)
                                    .Set("success", success)
                                    .Set("error", error));
                          },
                          id));
              installer->BeginInstall();
              std::move(reply).Run(base::Value(), std::string());
            },
            *id, widget ? widget->GetWeakPtr() : base::WeakPtr<views::Widget>(),
            std::move(reply)));
    return;
  }

  // Where Chrome's own tab-modal dialogs of the selected tab show, asked of
  // its Browser as Chrome asks it, for one { width, height } big: the
  // dialog's place, the most it may take, and where the page is, each from
  // the window's top left.
  if (name == "modalHost") {
    content::WebContents* page = window->selected();
    BrowserWindowInterface* browser =
        page ? GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(page)
             : nullptr;
    tabs::TabInterface* tab =
        page ? tabs::TabInterface::MaybeGetFromContents(page) : nullptr;
    web_modal::WebContentsModalDialogHost* host =
        browser && tab ? browser->GetWebContentsModalDialogHostForTab(tab)
                       : nullptr;
    if (!host) {
      std::move(reply).Run(base::Value(), "no tab-modal host");
      return;
    }
    const gfx::Size size(command.FindInt("width").value_or(260),
                         command.FindInt("height").value_or(100));
    const gfx::Point at = host->GetDialogPosition(size);
    const gfx::Size most = host->GetMaximumDialogSize();
    const gfx::Rect bounds = window->PageBounds();
    std::move(reply).Run(
        base::Value(base::DictValue()
                        .Set("position",
                             base::ListValue().Append(at.x()).Append(at.y()))
                        .Set("maximum", base::ListValue()
                                            .Append(most.width())
                                            .Append(most.height()))
                        .Set("page", base::ListValue()
                                         .Append(bounds.x())
                                         .Append(bounds.y())
                                         .Append(bounds.width())
                                         .Append(bounds.height()))
                        .Set("hostView", static_cast<bool>(host->GetHostView()))
                        .Set("activates", host->ShouldActivateDialog())),
        std::string());
    return;
  }

  // A button of the alert the window shows as a sheet, or of the app-modal
  // panel (the system's print panel), { title }, pressed as a click on it
  // does.
  if (name == "clickModalButton") {
    const std::string* title = command.FindString("title");
    if (!title || !PressSheetButton(widget, *title)) {
      std::move(reply).Run(base::Value(),
                           "no button " + (title ? *title : std::string()) +
                               " in the window's sheet or modal panel");
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
    // Where on the screen, when asked: { x, y } from its work area's top
    // left, or y "low", the window's top two thirds of the way down, where
    // the screen leaves more room above the toolbar than under it.
    const gfx::Rect work =
        display::Screen::Get()
            ->GetDisplayNearestWindow(widget->GetNativeWindow())
            .work_area();
    if (const std::optional<int> x = command.FindInt("x")) {
      bounds.set_x(work.x() + *x);
    }
    if (const std::string* low = command.FindString("y");
        low && *low == "low") {
      bounds.set_y(work.y() + work.height() * 2 / 3);
    } else if (const std::optional<int> y = command.FindInt("y")) {
      bounds.set_y(work.y() + *y);
    }
    const base::TimeTicks start = base::TimeTicks::Now();
    widget->SetBounds(bounds);
    const base::TimeDelta took = base::TimeTicks::Now() - start;
    const gfx::Rect page = window->PageBounds();
    const gfx::Rect client = widget->GetClientAreaBoundsInScreen();
    std::move(reply).Run(
        base::Value(
            base::DictValue()
                .Set("ms", took.InMillisecondsF())
                .Set("window", base::ListValue()
                                   .Append(client.width())
                                   .Append(client.height()))
                .Set("screen", base::ListValue()
                                   .Append(work.x())
                                   .Append(work.y())
                                   .Append(work.width())
                                   .Append(work.height()))
                .Set("frame",
                     base::ListValue().Append(client.x()).Append(client.y()))
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

  // The toolbar's extensions controls, by the Swift's names: { control,
  // id }. Each is a real click at the middle of its view, as a hand's, in
  // the background too: the puzzle, the menu's Manage all extensions, Visit
  // Chrome Web Store, an extension's row ("run"), pin, and more, and the more
  // menu's rows; and View Web Permissions, a pinned action's right-click
  // menu's, chosen as a click in the system's menu chooses it.
  if (name == "extensionsMenu") {
    const std::string* control = command.FindString("control");
    const std::string* id = command.FindString("id");
    if (!control) {
      std::move(reply).Run(base::Value(), "extensionsMenu needs a control");
      return;
    }
    if (*control == "viewWebPermissions") {
      ActionMenu* menu = id ? app_->Extensions().MakeActionMenu(*id) : nullptr;
      if (!menu || !menu->Choose(u"View Web Permissions")) {
        std::move(reply).Run(base::Value(),
                             "no View Web Permissions for that extension");
        return;
      }
      std::move(reply).Run(base::Value(), std::string());
      return;
    }
    const std::string suffix = id ? "." + *id : std::string();
    const std::map<std::string, std::string> names = {
        {"puzzle", "pane.extensions"},
        {"manage", "pane.extensionsMenu.manage"},
        {"webStore", "pane.extensionsMenu.webStore"},
        {"run", "pane.extensionsMenu.extension" + suffix},
        {"pin", "pane.extensionsMenu.pin" + suffix},
        {"more", "pane.extensionsMenu.more" + suffix},
        {"storePage", "pane.extensionMoreMenu.storePage"},
        {"remove", "pane.extensionMoreMenu.remove"},
        {"morePin", "pane.extensionMoreMenu.pin"},
        {"manageExtension", "pane.extensionMoreMenu.manage"},
        {"options", "pane.extensionMoreMenu.options"},
    };
    const auto found = names.find(*control);
    std::vector<std::pair<std::string, const views::View*>> named =
        window->root()->toolbar()->NamedViews();
    if (ExtensionsMenuView* menu = window->root()->extensions_menu()) {
      for (auto& entry : menu->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    if (ExtensionMoreMenuView* more = window->root()->extension_more_menu()) {
      for (auto& entry : more->NamedViews()) {
        named.push_back(std::move(entry));
      }
    }
    const views::View* view = nullptr;
    for (const auto& [view_name, each] : named) {
      if (found != names.end() && view_name == found->second &&
          each->IsDrawn()) {
        view = each;
      }
    }
    if (!view) {
      std::move(reply).Run(base::Value(),
                           "extensionsMenu: no " + *control + " shows");
      return;
    }
    const gfx::Rect frame = views::View::ConvertRectToTarget(
        view, widget->GetRootView(), view->GetLocalBounds());
    PostClick(widget, frame.CenterPoint().x(), frame.CenterPoint().y(), 1);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // The shown profile's extension's action, as its button runs it: its
  // popup, or chrome.action.onClicked (the Swift's runAction).
  if (name == "runExtensionAction") {
    const std::string* id = command.FindString("id");
    if (!id) {
      std::move(reply).Run(base::Value(), "runExtensionAction needs an id");
      return;
    }
    app_->Extensions().RunAction(*id);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  if (name == "closeExtensionPopup") {
    app_->Extensions().ClosePopup();
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // A shown extension's pinned action's right-click menu, made as a
  // right-click makes it: the extension's own items and its site access, as
  // the Swift's engine handed them over, and the whole menu; with `run`, the
  // item of that title chosen, as a click in the system's menu chooses it.
  if (name == "extensionActionMenu") {
    const std::string* id = command.FindString("id");
    ActionMenu* menu = id ? app_->Extensions().MakeActionMenu(*id) : nullptr;
    if (!menu) {
      std::move(reply).Run(base::Value(base::DictValue()
                                           .Set("items", base::ListValue())
                                           .Set("siteAccess", base::ListValue())
                                           .Set("menu", base::ListValue())),
                           std::string());
      return;
    }
    const ui::SimpleMenuModel& model = menu->model();
    base::DictValue described =
        base::DictValue()
            .Set("items", PageContextMenus::Describe(model, menu->items_begin(),
                                                     menu->items_end()))
            .Set("siteAccess", menu->site_access_index()
                                   ? PageContextMenus::Describe(
                                         model, *menu->site_access_index(),
                                         *menu->site_access_index() + 1)
                                   : base::ListValue())
            .Set("menu", PageContextMenus::Describe(model));
    if (const std::string* run = command.FindString("run")) {
      if (!menu->Choose(base::UTF8ToUTF16(*run))) {
        std::move(reply).Run(base::Value(), "no action menu item " + *run);
        return;
      }
    }
    std::move(reply).Run(base::Value(std::move(described)), std::string());
    return;
  }

  // An extension's pin, its being on, and its removal, in a profile, as the
  // Swift's channel set them in the engine: { id, profile, pinned | enabled }.
  if (name == "setExtensionPinned" || name == "setExtensionEnabled" ||
      name == "uninstallExtension") {
    const std::string* id = command.FindString("id");
    const std::string* profile_id = command.FindString("profile");
    Profile* profile = profile_id ? app_->ProfileIfOpen(*profile_id) : nullptr;
    if (!id || !profile) {
      std::move(reply).Run(base::Value(),
                           name + " needs an id and an open profile");
      return;
    }
    if (name == "setExtensionPinned") {
      app_->Extensions().SetPinned(*profile, *id,
                                   command.FindBool("pinned").value_or(true));
    } else if (name == "setExtensionEnabled") {
      auto* registrar = extensions::ExtensionRegistrar::Get(profile);
      if (command.FindBool("enabled").value_or(true)) {
        registrar->EnableExtension(*id);
      } else {
        registrar->DisableExtension(
            *id, {extensions::disable_reason::DISABLE_USER_ACTION});
      }
    } else {
      std::u16string error;
      extensions::ExtensionRegistrar::Get(profile)->UninstallExtension(
          *id, extensions::UNINSTALL_REASON_USER_INITIATED, &error);
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
