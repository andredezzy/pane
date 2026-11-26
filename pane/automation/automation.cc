// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/automation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/fixed_flat_map.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path_watcher.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/stack_allocated.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/execution_fence.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
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
#include "extensions/browser/extension_install_prompt_client.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/install_prompt_data.h"
#include "extensions/browser/uninstall_reason.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "pane/app/data_launch.h"
#include "pane/automation/channel_pointer.h"
#include "pane/automation/file_panels.h"
#include "pane/automation/input.h"
#include "pane/automation/system.h"
#include "pane/automation/windows.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profile_cache.h"
#include "pane/encryption/background_work.h"
#include "pane/model/app_settings.h"
#include "pane/model/model_store.h"
#include "pane/model/pin_entry.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/ui/app/menu_bar.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/menu_button.h"
#include "pane/ui/design/update_status.h"
#include "pane/ui/dialogs/about_dialog.h"
#include "pane/ui/dialogs/extension_prompt_view.h"
#include "pane/ui/dialogs/page_dialog.h"
#include "pane/ui/dialogs/system_alert_dialog.h"
#include "pane/ui/extensions/action_menu.h"
#include "pane/ui/extensions/action_popup.h"
#include "pane/ui/extensions/extension_actions_view.h"
#include "pane/ui/extensions/extensions_menu_view.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/lock/lock_screen_view.h"
#include "pane/ui/page/find_bar_view.h"
#include "pane/ui/page/page_context_menu.h"
#include "pane/ui/panels/app_icon_picker.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/render.h"
#include "pane/ui/sheets/profile_sheet.h"
#include "pane/ui/sidebar/profile_row.h"
#include "pane/ui/sidebar/sidebar_view.h"
#include "pane/ui/sidebar/tab_row.h"
#include "pane/ui/sidebar/update_pill.h"
#include "pane/ui/tabs/tab_switcher_view.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "pane/ui/window/window.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/models/menu_model.h"
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
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace pane {

namespace {

// A rect as the channel gives one: x, y, width, height.
base::ListValue RectList(const gfx::Rect& rect) {
  return base::ListValue()
      .Append(rect.x())
      .Append(rect.y())
      .Append(rect.width())
      .Append(rect.height());
}

// A view's frame in the window, from its top left.
base::ListValue FrameIn(const views::View* view, const views::View* root) {
  return RectList(
      views::View::ConvertRectToTarget(view, root, view->GetLocalBounds()));
}

// A real click in the middle of `view`, where it shows.
void ClickMiddle(views::Widget* widget, const views::View* view) {
  const gfx::Point middle =
      views::View::ConvertRectToTarget(view, widget->GetRootView(),
                                       view->GetLocalBounds())
          .CenterPoint();
  PostClick(widget, middle.x(), middle.y(), 1);
}

// The alert the window asks with, { title, message, buttons } from left to
// right: Pane's own where it draws the system's (Windows, Paper 11.13 A),
// else the system's sheet (the Mac's).
base::Value AlertOf(views::Widget* widget, RootView* root) {
  SystemAlertDialog* alert = root ? root->alert() : nullptr;
  if (!alert) {
    return SheetAlert(widget);
  }
  return base::Value(base::DictValue()
                         .Set("title", alert->alert().title)
                         .Set("message", alert->alert().message)
                         .Set("buttons", base::ListValue()
                                             .Append(alert->alert().cancel)
                                             .Append(alert->alert().action)));
}

// About Pane while it shows where Pane draws it (Windows, Paper 11.15 A): its
// lines, and whether its icon shows.
base::Value AboutOf(RootView* root) {
  AboutDialog* about = root ? root->about() : nullptr;
  if (!about) {
    return base::Value();
  }
  base::ListValue lines;
  for (const std::u16string& line : about->Lines()) {
    lines.Append(line);
  }
  return base::Value(base::DictValue()
                         .Set("lines", std::move(lines))
                         .Set("icon", about->has_icon()));
}

// The item of `menu` titled `title`, in either case: Chromium title-cases
// its menus' words on the Mac alone (its strings' use_titlecase). Never a
// separator.
std::optional<size_t> ItemTitled(const ui::MenuModel& menu,
                                 std::u16string_view title) {
  for (size_t index = 0; index < menu.GetItemCount(); ++index) {
    if (menu.GetTypeAt(index) != ui::MenuModel::TYPE_SEPARATOR &&
        base::EqualsCaseInsensitiveASCII(menu.GetLabelAt(index), title)) {
      return index;
    }
  }
  return std::nullopt;
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
  ChannelPointer::MovedTo(widget, std::lround(at.x()), std::lround(at.y()));
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
// channel describes one (ADR 0018).
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

// The key a scenario names ("\x1b" for Esc, "\r" for Return, "\t" for Tab,
// " ", "control", "shift", a letter, a digit, the punctuation of Pane's
// shortcuts, or the arrows, Home, End, and the function keys Windows'
// shortcuts take: "left", "right", "up", "down", "home", "end", "f4", "f6",
// "f11", "f12"), as Views codes it.
std::optional<ui::KeyboardCode> KeyCodeOf(std::string_view key) {
  if (key == "left") {
    return ui::VKEY_LEFT;
  }
  if (key == "right") {
    return ui::VKEY_RIGHT;
  }
  if (key == "up") {
    return ui::VKEY_UP;
  }
  if (key == "down") {
    return ui::VKEY_DOWN;
  }
  if (key == "home") {
    return ui::VKEY_HOME;
  }
  if (key == "end") {
    return ui::VKEY_END;
  }
  if (key == "f6") {
    return ui::VKEY_F6;
  }
  if (key == "f4") {
    return ui::VKEY_F4;
  }
  if (key == "f11") {
    return ui::VKEY_F11;
  }
  if (key == "f12") {
    return ui::VKEY_F12;
  }
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

int FlagsOf(const std::vector<std::string>& modifiers) {
  int flags = ui::EF_NONE;
  for (const std::string& name : modifiers) {
    if (name == "command") {
      flags |= ui::EF_COMMAND_DOWN;
    } else if (name == "shift") {
      flags |= ui::EF_SHIFT_DOWN;
    } else if (name == "control") {
      flags |= ui::EF_CONTROL_DOWN;
    } else if (name == "option") {
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

// A user's keys reach the window only while it is key, and as it becomes key
// Views gives the focus to the view it kept as the window last resigned key,
// or that asked for the focus while the window wasn't key
// (FocusManager::RestoreFocusedView, from NativeWidgetMac's key status). A
// scenario's copy in the background is not key: it was for a moment as it
// launched, AppKit making it active, until it gave the front back, or a view's
// request for the focus asked to activate it (Views does, while the app is
// active) and the app in use kept the front; either way Views left the focus
// on no view, kept for when the window is key. So the channel gives it back
// as becoming key would, without the front, and its keys then go where a
// user's would, through the window's own dispatch.
void FocusAsKeyWindow(views::Widget* widget) {
  views::FocusManager* focus = widget ? widget->GetFocusManager() : nullptr;
  if (focus && !widget->IsActive() && !focus->GetFocusedView()) {
    focus->RestoreFocusedView();
  }
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
  // How often, and for how long, a command that can't be read yet is looked
  // at again: Windows can hold a file the runner just moved into `in` for a
  // moment (the move's own handle, a scan), so the first read finds nothing.
  static constexpr base::TimeDelta kReadAgainAfter = base::Milliseconds(20);
  static constexpr int kReadsOfOneCommand = 100;

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
      const bool read = base::ReadFileToString(file, &text) && !text.empty();
      // A command is never empty: one not read yet waits, and so do those
      // sent after it, which run in their order.
      if (!read && ++reads_[file] < kReadsOfOneCommand) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(&Files::Changed, weak_factory_.GetWeakPtr(), in_,
                           false),
            kReadAgainAfter);
        return;
      }
      reads_.erase(file);
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
  // The commands not read yet, and how often each was tried.
  std::map<base::FilePath, int> reads_;
  base::WeakPtrFactory<Files> weak_factory_{this};
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

// The channel's commands, each its own function, in one table by its name
// (RowFor), each row saying what the command needs before it runs: Dispatch
// checks that, then runs it. A class of Automation's, so its commands reach
// what the channel keeps between them.
class Automation::Commands {
 public:
  // A command as it came, and the window as Dispatch found it, for as long as
  // Dispatch runs.
  struct Call {
    STACK_ALLOCATED();

   public:
    Automation& channel;
    UiApp& app;
    const std::string& name;
    const base::DictValue& command;
    Window* window;
    // Null while the window is closed.
    views::Widget* widget;
  };

  // What a command needs before it runs, each a bit of its row's `needs`,
  // checked in this order; without it, the command answers why.
  enum Needs : uint8_t {
    // The profiles loaded: an edit of them before they load, or behind the
    // lock, would edit a model that holds none of them, and the load would
    // take it back.
    kProfiles = 1 << 0,
    // The window open.
    kWindow = 1 << 1,
  };

  struct Row {
    void (*run)(const Call& call, Reply reply);
    uint8_t needs = 0;
  };

  // The row of the command named `name`, or null for none.
  static const Row* RowFor(std::string_view name);

 private:
  // Each command's handler, in the table's order.
  static void State(const Call& call, Reply reply);
  static void Navigate(const Call& call, Reply reply);
  static void ScreenReaderFocus(const Call& call, Reply reply);
  static void ClickNotification(const Call& call, Reply reply);
  static void Reload(const Call& call, Reply reply);
  static void CheckUpdates(const Call& call, Reply reply);
  static void PressUpdateControl(const Call& call, Reply reply);
  static void Activate(const Call& call, Reply reply);
  static void CreateProfile(const Call& call, Reply reply);
  static void OpenTab(const Call& call, Reply reply);
  static void MoveTabOrProfile(const Call& call, Reply reply);
  static void UnloadOrDeleteProfile(const Call& call, Reply reply);
  static void SetKeepLoaded(const Call& call, Reply reply);
  static void ReadProfile(const Call& call, Reply reply);
  static void SetProfileProxyOrFingerprint(const Call& call, Reply reply);
  static void SetSavesOrClearsHistory(const Call& call, Reply reply);
  static void ChangeSystemTimeZone(const Call& call, Reply reply);
  static void SetProxyStatus(const Call& call, Reply reply);
  static void CloseTab(const Call& call, Reply reply);
  static void ShowPage(const Call& call, Reply reply);
  static void Select(const Call& call, Reply reply);
  static void Find(const Call& call, Reply reply);
  static void CloseFind(const Call& call, Reply reply);
  static void FindAccessibility(const Call& call, Reply reply);
  static void ExtensionsAccessibility(const Call& call, Reply reply);
  static void ShowExclusiveAccessHint(const Call& call, Reply reply);
  static void ToggleDevTools(const Call& call, Reply reply);
  static void ChooseContextMenuItem(const Call& call, Reply reply);
  static void Switcher(const Call& call, Reply reply);
  static void SetSleepSettings(const Call& call, Reply reply);
  static void ReadCacheSizeOrCookies(const Call& call, Reply reply);
  static void SetDownloadDirectory(const Call& call, Reply reply);
  static void Save(const Call& call, Reply reply);
  static void Theme(const Call& call, Reply reply);
  static void AppIcon(const Call& call, Reply reply);
  static void AppIconPicker(const Call& call, Reply reply);
  static void ShowPinScreen(const Call& call, Reply reply);
  static void PinKeys(const Call& call, Reply reply);
  static void ResignKey(const Call& call, Reply reply);
  static void SetLockAfter(const Call& call, Reply reply);
  static void RequestEnhancedUserInterface(const Call& call, Reply reply);
  static void AppleScript(const Call& call, Reply reply);
  static void SystemEvent(const Call& call, Reply reply);
  static void LockNow(const Call& call, Reply reply);
  static void HoldBackgroundWork(const Call& call, Reply reply);
  static void WaitForBackgroundWork(const Call& call, Reply reply);
  static void Menu(const Call& call, Reply reply);
  static void MenuBar(const Call& call, Reply reply);
  static void Render(const Call& call, Reply reply);
  static void Key(const Call& call, Reply reply);
  static void ClickWindow(const Call& call, Reply reply);
  static void ClickPage(const Call& call, Reply reply);
  static void HoverWindow(const Call& call, Reply reply);
  static void ReleaseModifiers(const Call& call, Reply reply);
  static void HitTest(const Call& call, Reply reply);
  static void ScrollWindow(const Call& call, Reply reply);
  static void DragWindow(const Call& call, Reply reply);
  static void PointerWindow(const Call& call, Reply reply);
  static void Frames(const Call& call, Reply reply);
  static void SettingsMenu(const Call& call, Reply reply);
  static void DispatchKey(const Call& call, Reply reply);
  static void RowMenu(const Call& call, Reply reply);
  static void ClickSheetButton(const Call& call, Reply reply);
  static void FillSheet(const Call& call, Reply reply);
  static void DialogAccessibility(const Call& call, Reply reply);
  static void AnswerFileChooser(const Call& call, Reply reply);
  static void TypeInPage(const Call& call, Reply reply);
  static void WindowKey(const Call& call, Reply reply);
  static void InstallExtension(const Call& call, Reply reply);
  static void AskToAddExtension(const Call& call, Reply reply);
  static void PromptAnswer(const Call& call, Reply reply);
  static void ModalHost(const Call& call, Reply reply);
  static void ClickModalButton(const Call& call, Reply reply);
  static void ResizeWindow(const Call& call, Reply reply);
  static void PageLayout(const Call& call, Reply reply);
  static void OpenOrCloseProfileSheet(const Call& call, Reply reply);
  static void SheetField(const Call& call, Reply reply);
  static void SheetMenu(const Call& call, Reply reply);
  static void Accessibility(const Call& call, Reply reply);
  static void ExtensionsMenu(const Call& call, Reply reply);
  static void RunExtensionAction(const Call& call, Reply reply);
  static void CloseExtensionPopup(const Call& call, Reply reply);
  static void ExtensionActionMenu(const Call& call, Reply reply);
  static void PinEnableOrUninstallExtension(const Call& call, Reply reply);
  static void Quit(const Call& call, Reply reply);
};

Automation::Automation(const base::FilePath& directory, UiApp* app)
    : app_(app) {
  // The system's file panels, as a scenario sees and answers them.
  WatchFilePanels();
  // A download's dot at full, not breathing (ADR 0128).
  UpdateDot::HoldStill();
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
  Dispatch(
      command_name ? *command_name : std::string(), command,
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
            state.Set("alert", AlertOf(automation->app_->window()->widget(),
                                       automation->app_->window()->root()));
            state.Set("about", AboutOf(automation->app_->window()->root()));
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

// static
const Automation::Commands::Row* Automation::Commands::RowFor(
    std::string_view name) {
  static constexpr auto kTable = base::MakeFixedFlatMap<std::string_view, Row>({
      {"state", {&State}},
      {"navigate", {&Navigate}},
      {"screenReaderFocus", {&ScreenReaderFocus}},
      {"clickNotification", {&ClickNotification}},
      {"reload", {&Reload}},
      {"checkUpdates", {&CheckUpdates}},
      {"downloadUpdate", {&PressUpdateControl, kWindow}},
      {"restartToUpdate", {&PressUpdateControl, kWindow}},
      {"dismissUpdate", {&PressUpdateControl, kWindow}},
      {"activate", {&Activate}},
      {"createProfile", {&CreateProfile, kProfiles}},
      {"openTab", {&OpenTab, kProfiles}},
      {"moveTab", {&MoveTabOrProfile, kProfiles}},
      {"moveProfile", {&MoveTabOrProfile, kProfiles}},
      {"unloadProfile", {&UnloadOrDeleteProfile}},
      {"deleteProfile", {&UnloadOrDeleteProfile, kProfiles}},
      {"setKeepLoaded", {&SetKeepLoaded, kProfiles}},
      {"readProfile", {&ReadProfile, kProfiles}},
      {"setProfileProxy", {&SetProfileProxyOrFingerprint, kProfiles}},
      {"setProfileFingerprint", {&SetProfileProxyOrFingerprint, kProfiles}},
      {"setSavesHistory", {&SetSavesOrClearsHistory, kProfiles}},
      {"setClearsHistory", {&SetSavesOrClearsHistory, kProfiles}},
      {"changeSystemTimeZone", {&ChangeSystemTimeZone}},
      {"setProxyStatus", {&SetProxyStatus, kProfiles}},
      {"closeTab", {&CloseTab}},
      {"showPage", {&ShowPage}},
      {"select", {&Select}},
      {"find", {&Find}},
      {"closeFind", {&CloseFind}},
      {"findAccessibility", {&FindAccessibility}},
      {"extensionsAccessibility", {&ExtensionsAccessibility, kWindow}},
      {"showExclusiveAccessHint", {&ShowExclusiveAccessHint}},
      {"toggleDevTools", {&ToggleDevTools}},
      {"chooseContextMenuItem", {&ChooseContextMenuItem}},
      {"switcher", {&Switcher}},
      {"setSleepSettings", {&SetSleepSettings}},
      {"readCacheSize", {&ReadCacheSizeOrCookies}},
      {"readCookies", {&ReadCacheSizeOrCookies}},
      {"setDownloadDirectory", {&SetDownloadDirectory}},
      {"save", {&Save}},
      {"theme", {&Theme}},
      {"appIcon", {&AppIcon}},
      {"appIconPicker", {&AppIconPicker}},
      {"showPinScreen", {&ShowPinScreen, kWindow}},
      {"pinKeys", {&PinKeys, kWindow}},
      {"resignKey", {&ResignKey, kWindow}},
      {"setLockAfter", {&SetLockAfter}},
      {"requestEnhancedUserInterface", {&RequestEnhancedUserInterface}},
      {"appleScript", {&AppleScript}},
      {"systemEvent", {&SystemEvent}},
      {"lockNow", {&LockNow}},
      {"holdBackgroundWork", {&HoldBackgroundWork}},
      {"waitForBackgroundWork", {&WaitForBackgroundWork}},
      {"menu", {&Menu}},
      {"menuBar", {&MenuBar}},
      {"render", {&Render, kWindow}},
      {"key", {&Key, kWindow}},
      {"clickWindow", {&ClickWindow, kWindow}},
      {"clickPage", {&ClickPage, kWindow}},
      {"hoverWindow", {&HoverWindow, kWindow}},
      {"releaseModifiers", {&ReleaseModifiers, kWindow}},
      {"hitTest", {&HitTest, kWindow}},
      {"scrollWindow", {&ScrollWindow, kWindow}},
      {"dragWindow", {&DragWindow, kWindow}},
      {"pointerWindow", {&PointerWindow, kWindow}},
      {"frames", {&Frames, kWindow}},
      {"settingsMenu", {&SettingsMenu, kWindow}},
      {"dispatchKey", {&DispatchKey, kWindow}},
      {"rowMenu", {&RowMenu, kWindow}},
      {"clickSheetButton", {&ClickSheetButton, kWindow}},
      {"fillSheet", {&FillSheet, kWindow}},
      {"dialogAccessibility", {&DialogAccessibility, kWindow}},
      {"answerFileChooser", {&AnswerFileChooser}},
      {"typeInPage", {&TypeInPage, kWindow}},
      {"windowKey", {&WindowKey, kWindow}},
      {"installExtension", {&InstallExtension}},
      {"askToAddExtension", {&AskToAddExtension}},
      {"promptAnswer", {&PromptAnswer}},
      {"modalHost", {&ModalHost, kWindow}},
      {"clickModalButton", {&ClickModalButton, kWindow}},
      {"resizeWindow", {&ResizeWindow, kWindow}},
      {"pageLayout", {&PageLayout, kWindow}},
      {"openProfileSheet", {&OpenOrCloseProfileSheet, kWindow}},
      {"closeProfileSheet", {&OpenOrCloseProfileSheet, kWindow}},
      {"sheetField", {&SheetField, kWindow}},
      {"sheetMenu", {&SheetMenu, kWindow}},
      {"accessibility", {&Accessibility, kWindow}},
      {"extensionsMenu", {&ExtensionsMenu, kWindow}},
      {"runExtensionAction", {&RunExtensionAction}},
      {"closeExtensionPopup", {&CloseExtensionPopup}},
      {"extensionActionMenu", {&ExtensionActionMenu}},
      {"setExtensionPinned", {&PinEnableOrUninstallExtension}},
      {"setExtensionEnabled", {&PinEnableOrUninstallExtension}},
      {"uninstallExtension", {&PinEnableOrUninstallExtension}},
      {"quit", {&Quit}},
  });
  const auto row = kTable.find(name);
  return row == kTable.end() ? nullptr : &row->second;
}

// The guard: a command with no row is unknown, and one whose row needs what
// isn't so answers that, in Needs' order; else its handler runs, and replies.
void Automation::Dispatch(const std::string& name,
                          const base::DictValue& command,
                          Reply reply) {
  const Commands::Row* row = Commands::RowFor(name);
  if (!row) {
    std::move(reply).Run(base::Value(), "unknown command " + name);
    return;
  }
  if ((row->needs & Commands::kProfiles) &&
      !AppData::Get()->store().profiles_loaded()) {
    std::move(reply).Run(base::Value(), name + ": the profiles are not open");
    return;
  }
  Window* window = app_->window();
  views::Widget* widget = window->widget();
  if ((row->needs & Commands::kWindow) && !widget) {
    std::move(reply).Run(base::Value(), name + ": the window is closed");
    return;
  }
  row->run({*this, *app_, name, command, window, widget}, std::move(reply));
}

// Nothing but the state, which every reply carries.
// static
void Automation::Commands::State(const Call& call, Reply reply) {
  std::move(reply).Run(base::Value(), std::string());
}

// What the address field's Return does with `input`: the selected page goes
// there, or a new tab opens there (the Swift's navigate).
// static
void Automation::Commands::Navigate(const Call& call, Reply reply) {
  const std::string* input = call.command.FindString("input");
  if (!input) {
    std::move(reply).Run(base::Value(), "navigate needs input");
    return;
  }
  call.app.AddressEntered(base::UTF8ToUTF16(*input));
  std::move(reply).Run(base::Value(), std::string());
}

// A screen reader's focus on a control of the window, by its name
// ("pane.sidebar.resize"), as Views takes it from VoiceOver or Narrator: the
// keys then go to it.
// static
void Automation::Commands::ScreenReaderFocus(const Call& call, Reply reply) {
  const std::string* control = call.command.FindString("name");
  views::View* view = nullptr;
  if (control && call.widget) {
    for (int id = kViewIdSidebar; id <= kViewIdWindowClose && !view; ++id) {
      if (ViewIdName(id) == *control) {
        view = call.widget->GetRootView()->GetViewByID(id);
      }
    }
  }
  ui::AXActionData focus;
  focus.action = ax::mojom::Action::kFocus;
  if (!view || !view->HandleAccessibleAction(focus)) {
    std::move(reply).Run(base::Value(),
                         "screenReaderFocus: no control by that name takes "
                         "a screen reader's focus");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// A site's notification clicked in macOS's notification center, handed to
// the browser as the center hands it, with no click on the screen
// (notification-click). Windows' toasts come as another launch, which the
// runner hands as notification_helper.exe does.
// static
void Automation::Commands::ClickNotification(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profileId");
  const std::string* id = call.command.FindString("id");
  const std::string* origin = call.command.FindString("origin");
  if (!profile_id || !id || !origin) {
    std::move(reply).Run(base::Value(),
                         "clickNotification needs profileId, id, and origin");
    return;
  }
  if (!ClickSystemNotification(
          *profile_id, *id, *origin,
          call.command.FindBool("persistent").value_or(true))) {
    std::move(reply).Run(base::Value(),
                         "clickNotification: a toast's click comes as "
                         "another launch here");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The toolbar's Reload (a crashed tab's too), as its press runs it.
// static
void Automation::Commands::Reload(const Call& call, Reply reply) {
  call.app.Reload();
  std::move(reply).Run(base::Value(), std::string());
}

// The app menu's Check for Updates…, chosen as a click chooses it.
// static
void Automation::Commands::CheckUpdates(const Call& call, Reply reply) {
  pane::MenuBar::Native* menu_bar = call.app.menu_bar().native();
  if (!menu_bar || !menu_bar->Choose({u"Pane", u"Check for Updates\u2026"})
                        .value_or(false)) {
    std::move(reply).Run(base::Value(),
                         "checkUpdates: the app menu's item did not run");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The update's control, pressed as a user presses it, with a real click at
// its middle: the Lock's action while the Lock shows, which fades in under
// the pointer, else the pill's (Download, Restart to update, or Quit and
// install), else Settings' button; and the pill's dismiss, under the
// pointer too. Restart to update waits for a staged update and does
// nothing without one.
// static
void Automation::Commands::PressUpdateControl(const Call& call, Reply reply) {
  if (call.name == "restartToUpdate" && !call.app.updater().staged()) {
    std::move(reply).Run(base::Value(), std::string());
    return;
  }
  // Where the control is once laid out, as the user sees it: a pill the
  // update just brought has its place only after the next layout, and its
  // middle measured before was another view's (Windows' runs found it).
  call.widget->LayoutRootViewIfNecessary();
  RootView* root = call.window->root();
  const bool locked = root->lock_screen()->showing();
  std::vector<int> ids;
  if (call.name == "dismissUpdate") {
    ids = {kViewIdUpdateDismiss};
  } else if (locked) {
    ids = {kViewIdLockScreenDownloadUpdate, kViewIdLockScreenRestartToUpdate,
           kViewIdLockScreenQuitAndInstall};
  } else {
    ids = {kViewIdUpdate, kViewIdSettingsDownloadUpdate};
  }
  // A pill on its way out takes no press.
  const UpdatePill* pill = root->sidebar()->update_pill();
  const bool pill_shown = pill && pill->GetVisible() && pill->shown();
  const views::View* control = nullptr;
  for (const int id : ids) {
    const views::View* view = root->GetViewByID(id);
    if (view && view->IsDrawn() && view->GetEnabled() &&
        (pill_shown || (id != kViewIdUpdate && id != kViewIdUpdateDismiss))) {
      control = view;
      break;
    }
  }
  if (!control) {
    std::move(reply).Run(base::Value(),
                         call.name + ": no update control shows");
    return;
  }
  const gfx::Point middle =
      views::View::ConvertRectToTarget(control, call.widget->GetRootView(),
                                       control->GetLocalBounds())
          .CenterPoint();
  // The pointer over it first, which shows what shows only under it.
  if (call.name == "dismissUpdate" || locked) {
    PostMove(call.widget, middle.x(), middle.y());
  }
  PostClick(call.widget, middle.x(), middle.y(), 1);
  std::move(reply).Run(base::Value(), std::string());
}

// What a click on the window does first: it becomes the key window, the app
// in front, which a scenario's copy takes only when the runner allows it
// (run.ts --foreground); and a window closed opens again, as the Dock's
// click shows it.
// static
void Automation::Commands::Activate(const Call& call, Reply reply) {
  call.app.ShowWindow();
  call.window->Activate();
  std::move(reply).Run(base::Value(), std::string());
}

// The profile sheet's Create, as filled in: { name, color?, spoof?,
// savesHistory? }. Scenarios' profiles spoof nothing unless asked, so pages
// see the engine's own values. The sheet has no Group (the user's answer of
// 2026-11-11).
// static
void Automation::Commands::CreateProfile(const Call& call, Reply reply) {
  const std::string* profile_name = call.command.FindString("name");
  if (!profile_name) {
    std::move(reply).Run(base::Value(), "createProfile needs name");
    return;
  }
  model::ProfileDraft draft;
  draft.name = *profile_name;
  if (const std::string* color = call.command.FindString("color")) {
    draft.color =
        model::ColorNamed(*color).value_or(model::ProfileColor::kBlue);
  }
  draft.spoofing = call.command.FindBool("spoof").value_or(false);
  draft.saves_history = call.command.FindBool("savesHistory").value_or(true);
  if (draft.Validate()) {
    std::move(reply).Run(base::Value(), "createProfile needs a name");
    return;
  }
  std::move(reply).Run(base::Value(call.app.CreateProfile(draft)),
                       std::string());
}

// A new tab, selected: { profile, url? }, at the start page without a URL.
// static
void Automation::Commands::OpenTab(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  if (!profile_id) {
    std::move(reply).Run(base::Value(), "openTab needs profile");
    return;
  }
  std::optional<GURL> url;
  if (const std::string* spec = call.command.FindString("url")) {
    if (GURL parsed(*spec); parsed.is_valid()) {
      url = std::move(parsed);
    }
  }
  std::move(reply).Run(base::Value(call.app.OpenTab(*profile_id, url)),
                       std::string());
}

// A drag in the sidebar, as it lands: { profile, from, to } for a tab,
// { from, to } for a profile.
// static
void Automation::Commands::MoveTabOrProfile(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  const std::optional<int> from = call.command.FindInt("from");
  const std::optional<int> to = call.command.FindInt("to");
  if (call.name == "moveTab" && !profile_id) {
    std::move(reply).Run(base::Value(), "moveTab needs profile");
    return;
  }
  if (!from || !to) {
    std::move(reply).Run(base::Value(),
                         call.name + (from ? " needs to" : " needs from"));
    return;
  }
  // The drop's intents, as a drag's lands.
  if (call.name == "moveTab") {
    call.app.MoveTab(*profile_id, *from, *to);
  } else {
    call.app.MoveProfile(*from, *to);
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The sidebar's header on an expanded profile: it collapses, its pages
// unloading.
// static
void Automation::Commands::UnloadOrDeleteProfile(const Call& call,
                                                 Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  if (!profile_id) {
    std::move(reply).Run(base::Value(), call.name + " needs profile");
    return;
  }
  if (call.name == "unloadProfile") {
    call.app.CollapseProfile(*profile_id);
  } else {
    // The alert's Delete, past the alert, which `rowMenu` and
    // `clickModalButton` press.
    call.app.DeleteProfile(*profile_id);
  }
  std::move(reply).Run(base::Value(), std::string());
}

// A profile's menu's Keep loaded: { profile, keepLoaded? }.
// static
void Automation::Commands::SetKeepLoaded(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  if (!profile_id) {
    std::move(reply).Run(base::Value(), "setKeepLoaded needs profile");
    return;
  }
  call.app.SetKeepLoaded(*profile_id,
                         call.command.FindBool("keepLoaded").value_or(true));
  std::move(reply).Run(base::Value(), std::string());
}

// { profile }: the profile as the sealed profiles.json holds it.
// static
void Automation::Commands::ReadProfile(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  const model::BrowserProfile* profile =
      profile_id ? call.app.model().FindProfile(*profile_id) : nullptr;
  if (!profile) {
    std::move(reply).Run(base::Value(), "no such profile");
    return;
  }
  std::move(reply).Run(profile->ToValue(), std::string());
}

// A profile's proxy, { profile, proxy: { proxyType, host, port, username?,
// password? } | null }, or its fingerprint, { profile, fingerprint:
// profiles.json's | null }, read as profiles.json is, then applied as an
// edit applies it: a proxy applied to the open profile and tested, a
// fingerprint set and its pages made again.
// static
void Automation::Commands::SetProfileProxyOrFingerprint(const Call& call,
                                                        Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  const model::BrowserProfile* profile =
      profile_id ? call.app.model().FindProfile(*profile_id) : nullptr;
  const std::string key =
      call.name == "setProfileProxy" ? "proxy" : "fingerprint";
  const base::Value* value = call.command.Find(key);
  if (!profile || !value) {
    std::move(reply).Run(base::Value(),
                         call.name + " needs profile and " + key);
    return;
  }
  base::Value edited = profile->ToValue();
  edited.GetDict().Set(key, value->Clone());
  const std::optional<model::BrowserProfile> read =
      model::BrowserProfile::FromValue(edited);
  if (!read) {
    std::move(reply).Run(base::Value(),
                         "a " + key + " profiles.json can't read");
    return;
  }
  call.app.UpdateProfile(*profile_id, [&](model::BrowserProfile& updated) {
    if (key == "proxy") {
      updated.proxy = read->proxy;
    } else {
      updated.fingerprint = read->fingerprint;
    }
  });
  std::move(reply).Run(base::Value(), std::string());
}

// The profile sheet's Save history, as Save applies it: { profile,
// savesHistory }; and a clear still owed, as a lock or a quit leaves one:
// { profile }.
// static
void Automation::Commands::SetSavesOrClearsHistory(const Call& call,
                                                   Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  if (!profile_id || !call.app.model().FindProfile(*profile_id)) {
    std::move(reply).Run(base::Value(), call.name + " needs profile");
    return;
  }
  const bool saves = call.command.FindBool("savesHistory").value_or(true);
  call.app.UpdateProfile(*profile_id, [&](model::BrowserProfile& profile) {
    if (call.name == "setSavesHistory") {
      profile.SetSavesHistory(saves);
    } else {
      profile.clears_history = true;
    }
  });
  std::move(reply).Run(base::Value(), std::string());
}

// { zone }: the computer's zone changes, as far as the engine can tell
// (system.h).
// static
void Automation::Commands::ChangeSystemTimeZone(const Call& call, Reply reply) {
  const std::string* zone = call.command.FindString("zone");
  if (!zone) {
    std::move(reply).Run(base::Value(), "changeSystemTimeZone needs zone");
    return;
  }
  if (!pane::ChangeSystemTimeZone(*zone)) {
    std::move(reply).Run(base::Value(),
                         "changeSystemTimeZone: the system's zone is the "
                         "computer's alone here, for no app to change");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The sidebar's dot in a state of the proxy test's, { profile, status:
// "testing" | "connected" | "failed" | null }, for its renders: the test
// itself (ProfileNetwork) sets it as it runs and answers.
// static
void Automation::Commands::SetProxyStatus(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  const std::string* status = call.command.FindString("status");
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
  call.app.model().SetProxyStatus(*profile_id, named);
  std::move(reply).Run(base::Value(), std::string());
}

// A tab's X, or its menu's Close tab: its page asks first (beforeunload).
// static
void Automation::Commands::CloseTab(const Call& call, Reply reply) {
  const std::string* tab = call.command.FindString("tab");
  if (!tab) {
    std::move(reply).Run(base::Value(), "closeTab needs tab");
    return;
  }
  call.app.CloseTab(*tab);
  std::move(reply).Run(base::Value(), std::string());
}

// { page: "browser" | "settings" }: the sidebar's Settings, or back.
// static
void Automation::Commands::ShowPage(const Call& call, Reply reply) {
  const std::string* page = call.command.FindString("page");
  if (!page || (*page != "browser" && *page != "settings")) {
    std::move(reply).Run(base::Value(), "unknown page");
    return;
  }
  call.app.ShowPage(*page == "settings" ? model::BrowserModel::Page::kSettings
                                        : model::BrowserModel::Page::kBrowser);
  std::move(reply).Run(base::Value(), std::string());
}

// static
void Automation::Commands::Select(const Call& call, Reply reply) {
  const std::string* tab = call.command.FindString("tab");
  if (!tab) {
    std::move(reply).Run(base::Value(), "select needs tab");
    return;
  }
  call.app.Select(*tab);
  std::move(reply).Run(base::Value(), std::string());
}

// Find in page: { text, forward } shows the bar
// (⌘F's own path) and finds `text` as typed into its field, or, the text it
// holds already, steps to the next match, or the previous one (Return,
// Shift-Return).
// static
void Automation::Commands::Find(const Call& call, Reply reply) {
  const std::string* text = call.command.FindString("text");
  if (!text) {
    std::move(reply).Run(base::Value(), "find needs text");
    return;
  }
  RootView* root = call.window->root();
  const bool opens = root && !root->find_state().visible;
  if (opens) {
    call.app.ShowFind();
  }
  if (!root || !root->find_bar()) {
    std::move(reply).Run(base::Value(), "find needs a page");
    return;
  }
  const std::u16string query = base::UTF8ToUTF16(*text);
  if (root->find_state().text == query) {
    call.app.FindStep(call.command.FindBool("forward").value_or(true));
  } else {
    root->find_bar()->Type(query);
  }
  // Found as the bar shows, the field takes the keys with all of its text
  // selected, as ⌘F gives it them.
  if (opens) {
    root->find_bar()->field()->SelectAll(false);
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The find bar's close.
// static
void Automation::Commands::CloseFind(const Call& call, Reply reply) {
  call.app.CloseFind();
  std::move(reply).Run(base::Value(), std::string());
}

// What VoiceOver reads of find in page's bar while it shows (ADR 0071):
// each part's role and name.
// static
void Automation::Commands::FindAccessibility(const Call& call, Reply reply) {
  FindBarView* find =
      call.window->root() ? call.window->root()->find_bar() : nullptr;
  if (!find) {
    std::move(reply).Run(base::Value(), "no find bar");
    return;
  }
  base::DictValue parts;
  for (const auto& [part, view] : find->NamedViews()) {
    ui::AXNodeData data;
    view->GetViewAccessibility().GetAccessibleNodeData(&data);
    parts.Set(part,
              base::DictValue()
                  .Set("role", ui::ToString(data.role))
                  .Set("name", data.GetStringAttribute(
                                   ax::mojom::StringAttribute::kName))
                  .Set("ignored", view->GetViewAccessibility().GetIsIgnored()));
  }
  std::move(reply).Run(base::Value(std::move(parts)), std::string());
}

// What VoiceOver reads of the toolbar's extensions and the menus showing
// (ADR 0071): each named view's role, name, and whether it is selected.
// static
void Automation::Commands::ExtensionsAccessibility(const Call& call,
                                                   Reply reply) {
  std::vector<std::pair<std::string, const views::View*>> named;
  for (auto& entry :
       call.window->root()->toolbar()->extension_actions()->NamedViews()) {
    named.push_back(std::move(entry));
  }
  if (ExtensionsMenuView* menu = call.window->root()->extensions_menu()) {
    for (auto& entry : menu->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  if (ExtensionMoreMenuView* more =
          call.window->root()->extension_more_menu()) {
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
}

// The full screen's hint as Chrome's hook shows it (WindowHost's
// ShowExclusiveAccessHint), { hint }, empty for none: its look and fade,
// drawn without the front a page's fullscreen needs.
// static
void Automation::Commands::ShowExclusiveAccessHint(const Call& call,
                                                   Reply reply) {
  const std::string* hint = call.command.FindString("hint");
  if (!hint) {
    std::move(reply).Run(base::Value(), "showExclusiveAccessHint needs hint");
    return;
  }
  call.window->ShowExclusiveAccessHint(base::UTF8ToUTF16(*hint));
  std::move(reply).Run(base::Value(), std::string());
}

// View › Developer Tools (⇧⌘I).
// static
void Automation::Commands::ToggleDevTools(const Call& call, Reply reply) {
  call.app.ToggleDevTools();
  std::move(reply).Run(base::Value(), std::string());
}

// An item of the page's menu shown last, by its title, as a click in the
// menu chooses it: the platform's menu closes, and the menu is let go.
// static
void Automation::Commands::ChooseContextMenuItem(const Call& call,
                                                 Reply reply) {
  const std::string* title = call.command.FindString("title");
  if (!title || !call.app.context_menus().Choose(base::UTF8ToUTF16(*title))) {
    std::move(reply).Run(base::Value(), "no context menu item " +
                                            (title ? *title : std::string()));
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The tab switcher, as its keys run it: { action: "next" | "previous" }
// opens it or moves its selection, "commit" is Ctrl's release, "cancel" is
// Esc.
// static
void Automation::Commands::Switcher(const Call& call, Reply reply) {
  const std::string* action = call.command.FindString("action");
  if (!action || (*action != "next" && *action != "previous" &&
                  *action != "commit" && *action != "cancel")) {
    std::move(reply).Run(base::Value(), "unknown switcher action");
    return;
  }
  if (*action == "next" || *action == "previous") {
    call.app.SwitchTab(/*backward=*/*action == "previous");
  } else if (*action == "commit") {
    call.app.CommitSwitcher();
  } else {
    call.app.CancelSwitcher();
  }
  std::move(reply).Run(base::Value(), std::string());
}

// Settings' Sleep: { tabSleepAfterMinutes, profileUnloadAfterMinutes,
// cacheBudgetMB }, each a number, or null for off.
// static
void Automation::Commands::SetSleepSettings(const Call& call, Reply reply) {
  call.app.ChangeSettings([&](model::AppSettings& settings) {
    settings.tab_sleep_after_minutes =
        call.command.FindInt("tabSleepAfterMinutes");
    settings.profile_unload_after_minutes =
        call.command.FindInt("profileUnloadAfterMinutes");
    settings.cache_budget_mb = call.command.FindInt("cacheBudgetMB");
  });
  std::move(reply).Run(base::Value(), std::string());
}

// { profile }: an open profile's HTTP cache's bytes, or its cookies, read
// in the background; the replies' profiles[].cacheSize and
// profiles[].cookies hold them once read.
// static
void Automation::Commands::ReadCacheSizeOrCookies(const Call& call,
                                                  Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  Profile* profile = profile_id ? call.app.ProfileIfOpen(*profile_id) : nullptr;
  if (!profile) {
    std::move(reply).Run(base::Value(),
                         profile_id ? "profile " + *profile_id + " is not open"
                                    : call.name + " needs profile");
    return;
  }
  if (call.name == "readCacheSize") {
    call.channel.cache_sizes_.erase(*profile_id);
    ComputeCacheSize(profile,
                     base::BindOnce(
                         [](base::WeakPtr<Automation> automation,
                            std::string profile_id, int64_t bytes) {
                           if (automation) {
                             automation->cache_sizes_[profile_id] = bytes;
                           }
                         },
                         call.channel.weak_factory_.GetWeakPtr(), *profile_id));
  } else {
    call.channel.cookies_.erase(*profile_id);
    profile->GetDefaultStoragePartition()
        ->GetCookieManagerForBrowserProcess()
        ->GetAllCookies(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(
                [](base::WeakPtr<Automation> automation, std::string profile_id,
                   const net::CookieList& cookies) {
                  if (!automation) {
                    return;
                  }
                  base::ListValue described;
                  for (const net::CanonicalCookie& cookie : cookies) {
                    described.Append(DescribeCookie(cookie));
                  }
                  automation->cookies_[profile_id] = std::move(described);
                },
                call.channel.weak_factory_.GetWeakPtr(), *profile_id),
            net::CookieList()));
  }
  std::move(reply).Run(base::Value(), std::string());
}

// { profile, path }: where an open profile's downloads go, as its download
// preferences hold it.
// static
void Automation::Commands::SetDownloadDirectory(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  const std::string* path = call.command.FindString("path");
  if (!profile_id || !path) {
    std::move(reply).Run(base::Value(),
                         "setDownloadDirectory needs profile and path");
    return;
  }
  Profile* profile = call.app.ProfileIfOpen(*profile_id);
  if (!profile) {
    std::move(reply).Run(base::Value(),
                         "profile " + *profile_id + " is not open");
    return;
  }
  DownloadPrefs::FromBrowserContext(profile)->SetDownloadPath(
      base::FilePath::FromUTF8Unsafe(*path));
  std::move(reply).Run(base::Value(), std::string());
}

// What changed, written: the reply comes once it is on the disk.
// static
void Automation::Commands::Save(const Call& call, Reply reply) {
  call.app.SaveNow(base::BindOnce(
      [](Reply reply) { std::move(reply).Run(base::Value(), std::string()); },
      std::move(reply)));
}

// static
void Automation::Commands::Theme(const Call& call, Reply reply) {
  const std::string* theme_name = call.command.FindString("theme");
  if (!theme_name) {
    std::move(reply).Run(base::Value(), "theme needs theme");
    return;
  }
  const std::optional<model::Theme> theme = model::ThemeNamed(*theme_name);
  if (!theme) {
    std::move(reply).Run(base::Value(), "unknown theme");
    return;
  }
  call.app.ChangeSettings(
      [&](model::AppSettings& settings) { settings.theme = *theme; });
  std::move(reply).Run(base::Value(), std::string());
}

// { appIcon: "automatic" | "day" | … }: the App icon, as the picker's sky
// sets it, at any time, as the theme's.
// static
void Automation::Commands::AppIcon(const Call& call, Reply reply) {
  const std::string* icon_name = call.command.FindString("appIcon");
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
  call.app.ChangeSettings(
      [&](model::AppSettings& settings) { settings.app_icon = *icon; });
  std::move(reply).Run(base::Value(), std::string());
}

// { open }: Settings' App icon, as its press opens the picker, or the
// picker's Done.
// static
void Automation::Commands::AppIconPicker(const Call& call, Reply reply) {
  if (call.command.FindBool("open").value_or(true)) {
    call.app.OpenAppIconPicker();
  } else {
    call.app.CloseAppIconPicker();
  }
  std::move(reply).Run(base::Value(), std::string());
}

// Settings' Set up, Change, or Remove PIN, { mode }, as a user opens the
// PIN screen: Settings shown by a real click on the sidebar's Settings
// where it isn't, its button scrolled into view and pressed by a real
// click. The reply comes once the PIN screen shows.
// static
void Automation::Commands::ShowPinScreen(const Call& call, Reply reply) {
  const std::string* mode = call.command.FindString("mode");
  std::optional<model::PinEntry::Mode> pin_mode;
  if (mode && *mode == "setup") {
    pin_mode = model::PinEntry::Mode::kSetup;
  } else if (mode && *mode == "change") {
    pin_mode = model::PinEntry::Mode::kChange;
  } else if (mode && *mode == "remove") {
    pin_mode = model::PinEntry::Mode::kRemove;
  }
  if (!pin_mode) {
    std::move(reply).Run(base::Value(),
                         "showPinScreen needs mode: setup, change, or remove");
    return;
  }
  if (AppData::Get()->PinScreen() || !call.app.InteractionAllowed()) {
    std::move(reply).Run(base::Value(),
                         "showPinScreen: the PIN screen shows already, or "
                         "Pane is locked");
    return;
  }
  call.channel.OpenPinScreen(*pin_mode, base::TimeTicks::Now(), /*step=*/0,
                             std::move(reply));
}

// { keys: "1234" }, typed as a user types them, through the window to the
// PIN screen that has its keys, in the background too: digits, "<" Delete,
// ">" Return (Continue), "^" Esc (back).
// static
void Automation::Commands::PinKeys(const Call& call, Reply reply) {
  const std::string* keys = call.command.FindString("keys");
  if (!keys) {
    std::move(reply).Run(base::Value(), "pinKeys needs keys");
    return;
  }
  LockScreenView* lock = call.window->root()->lock_screen();
  if (!lock->takes_every_key()) {
    std::move(reply).Run(base::Value(), "pinKeys: no PIN screen shows");
    return;
  }
  FocusAsKeyWindow(call.widget);
  if (call.window->root()->focus() != ViewIdName(kViewIdLockScreen)) {
    std::move(reply).Run(base::Value(),
                         "pinKeys: the PIN screen doesn't have the keys: " +
                             call.window->root()->focus());
    return;
  }
  for (const char key : *keys) {
    const std::string typed = key == '<'   ? "\x7f"
                              : key == '>' ? "\r"
                              : key == '^' ? "\x1b"
                                           : std::string(1, key);
    if (!SendKeyToWindow(call.widget, typed)) {
      std::move(reply).Run(base::Value(), "pinKeys: no key code for that key");
      return;
    }
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The window resigns key, as when the app in use takes the front back
// (ResignKey): Views clears its focus, kept for when the window is key.
// static
void Automation::Commands::ResignKey(const Call& call, Reply reply) {
  if (!pane::ResignKey(call.widget)) {
    std::move(reply).Run(base::Value(), "resignKey: no window delegate");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// Lock after, { minutes }: a number, or null for Never, as Settings' menu
// sets it; the channel stands in for the menu (ADR 0018).
// static
void Automation::Commands::SetLockAfter(const Call& call, Reply reply) {
  const std::optional<int> minutes = call.command.FindInt("minutes");
  call.app.ChangeSettings([&](model::AppSettings& settings) {
    settings.lock_after_minutes = minutes;
  });
  std::move(reply).Run(base::Value(), std::string());
}

// What an assistive app sets on the application to ask for the whole
// accessibility tree (data-volume: Chromium answers it on a timer).
// static
void Automation::Commands::RequestEnhancedUserInterface(const Call& call,
                                                        Reply reply) {
  pane::RequestEnhancedUserInterface();
  std::move(reply).Run(base::Value(), std::string());
}

// { source }: a script run in the app, against itself (system.h), its
// result as text or null (applescript, ADR 0112).
// static
void Automation::Commands::AppleScript(const Call& call, Reply reply) {
  const std::string* source = call.command.FindString("source");
  if (!source) {
    std::move(reply).Run(base::Value(), "appleScript needs source");
    return;
  }
  const auto ran = RunAppleScript(*source);
  if (!ran.has_value()) {
    std::move(reply).Run(base::Value(), ran.error());
    return;
  }
  std::move(reply).Run(ran.value() ? base::Value(*ran.value()) : base::Value(),
                       std::string());
}

// What the system says that locks Pane by itself, { event }: "screensSleep"
// or "sessionResign", posted as the system posts it, to Pane alone
// (NSWorkspace's own center). The screen's lock and the computer's sleep
// reach every app, the user's own Pane among them, so no scenario posts
// them; pane_unittests covers them.
// static
void Automation::Commands::SystemEvent(const Call& call, Reply reply) {
  const std::string* event = call.command.FindString("event");
  if (!event || !PostSystemEvent(*event)) {
    std::move(reply).Run(base::Value(),
                         "systemEvent needs screensSleep or sessionResign");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// Lock Pane (⌃⌘L, Paper 07.9).
// static
void Automation::Commands::LockNow(const Call& call, Reply reply) {
  call.app.LockNow();
  std::move(reply).Run(base::Value(), std::string());
}

// The thread pool's BEST_EFFORT work waits { ms } from now, as Chrome's
// startup or a loaded Mac holds it, Chrome's for a profile opening then
// among it: what the Lock must let run before it drops the keys
// (lock-while-open, ADR 0116). Chromium's own fence, the one its startup
// takes.
// static
void Automation::Commands::HoldBackgroundWork(const Call& call, Reply reply) {
  const std::optional<int> ms = call.command.FindInt("ms");
  if (!ms || *ms <= 0) {
    std::move(reply).Run(base::Value(), "holdBackgroundWork needs ms");
    return;
  }
  if (!call.channel.background_held_) {
    call.channel.background_held_ =
        std::make_unique<base::ScopedBestEffortExecutionFence>();
  }
  // The timer is this channel's: it goes with it, the fence too.
  call.channel.background_release_.Start(
      FROM_HERE, base::Milliseconds(*ms),
      base::BindOnce(
          [](Automation* automation) { automation->background_held_.reset(); },
          base::Unretained(&call.channel)));
  std::move(reply).Run(base::Value(), std::string());
}

// Replies once the thread pool has run all it holds, as the Lock waits for
// it before its detach: { ran }, false past { ms } (30 s by default).
// static
void Automation::Commands::WaitForBackgroundWork(const Call& call,
                                                 Reply reply) {
  encryption::WaitForBackgroundWork(
      base::Milliseconds(call.command.FindInt("ms").value_or(30'000)),
      base::BindOnce(
          [](Reply reply, bool ran) {
            std::move(reply).Run(base::Value(ran), std::string());
          },
          std::move(reply)));
}

// An item of the menu bar chosen by its path, { path: ["File", "New
// Tab"] }, as a click on it chooses it: through the platform's own menu
// bar, its enabled states and titles brought up to date first, as when it
// opens. Whether it ran: false where the menu disables it (behind the
// Lock, all but Quit and Check for Updates…).
// static
void Automation::Commands::Menu(const Call& call, Reply reply) {
  const base::ListValue* path = call.command.FindList("path");
  std::vector<std::u16string> items;
  if (path) {
    for (const base::Value& item : *path) {
      items.push_back(item.is_string() ? base::UTF8ToUTF16(item.GetString())
                                       : std::u16string());
    }
  }
  pane::MenuBar::Native* menu_bar = call.app.menu_bar().native();
  const std::optional<bool> ran =
      menu_bar ? menu_bar->Choose(items) : std::nullopt;
  if (!ran) {
    std::move(reply).Run(base::Value(), "no such menu item");
    return;
  }
  std::move(reply).Run(base::Value(*ran), std::string());
}

// The menu bar as it shows now: each menu's items, with their titles,
// keys, and whether each is enabled, hidden, or checked.
// static
void Automation::Commands::MenuBar(const Call& call, Reply reply) {
  pane::MenuBar::Native* menu_bar = call.app.menu_bar().native();
  std::move(reply).Run(
      base::Value(menu_bar ? menu_bar->Describe() : base::ListValue()),
      std::string());
}

// The window drawn to a PNG, the page left out (pane/ui/render.h), at the
// screen's scale.
// static
void Automation::Commands::Render(const Call& call, Reply reply) {
  const std::string* path = call.command.FindString("path");
  // An action's popup renders from its own window: { target: "popup" }.
  const std::string* target = call.command.FindString("target");
  views::Widget* drawn = call.widget;
  if (target && *target == "popup") {
    ActionPopup* popup = call.app.Extensions().popup();
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
  call.channel.files_.AsyncCall(&Files::WriteRender)
      .WithArgs(base::FilePath::FromUTF8Unsafe(*path), std::move(*png))
      .Then(base::BindOnce(
          [](Reply reply) {
            std::move(reply).Run(base::Value(), std::string());
          },
          std::move(reply)));
}

// Real keys, through the window's event path (pane/automation/input.h).
// static
void Automation::Commands::Key(const Call& call, Reply reply) {
  const std::string* key = call.command.FindString("key");
  if (!key || !PostKey(call.widget, *key, Modifiers(call.command))) {
    std::move(reply).Run(base::Value(), "no key code for that key");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// A real click at a point of the window (from its top left).
// static
void Automation::Commands::ClickWindow(const Call& call, Reply reply) {
  const std::optional<double> x = call.command.FindDouble("x");
  const std::optional<double> y = call.command.FindDouble("y");
  if (!x || !y) {
    std::move(reply).Run(base::Value(), "clickWindow needs x and y");
    return;
  }
  PostClick(call.widget, *x, *y, call.command.FindInt("clicks").value_or(1));
  std::move(reply).Run(base::Value(), std::string());
}

// A real click on the shown page, at a point of its view (from its top
// left).
// static
void Automation::Commands::ClickPage(const Call& call, Reply reply) {
  const std::optional<double> x = call.command.FindDouble("x");
  const std::optional<double> y = call.command.FindDouble("y");
  if (!x || !y || !call.window->selected()) {
    std::move(reply).Run(base::Value(), !x || !y ? "clickPage needs x and y"
                                                 : "clickPage needs a page");
    return;
  }
  const gfx::Rect page = call.window->PageBounds();
  PostClick(call.widget, page.x() + *x, page.y() + *y, 1);
  std::move(reply).Run(base::Value(), std::string());
}

// The pointer over a point of the window (from its top left), as a mouse
// move the window dispatches; the real pointer stays where it is.
// static
void Automation::Commands::HoverWindow(const Call& call, Reply reply) {
  const std::optional<double> x = call.command.FindDouble("x");
  const std::optional<double> y = call.command.FindDouble("y");
  if (!x || !y) {
    std::move(reply).Run(base::Value(), "hoverWindow needs x and y");
    return;
  }
  PostMove(call.widget, *x, *y);
  std::move(reply).Run(base::Value(), std::string());
}

// Every modifier the channel holds down, let go (input.h).
// static
void Automation::Commands::ReleaseModifiers(const Call& call, Reply reply) {
  PostModifiers(call.widget, {});
  std::move(reply).Run(base::Value(), std::string());
}

// What a press at a point of the window (from its top left) would reach:
// the view Views would hand it, and whether the page or Views takes it
// (frame.h). A question, not input: it runs without the front.
// static
void Automation::Commands::HitTest(const Call& call, Reply reply) {
  const std::optional<double> x = call.command.FindDouble("x");
  const std::optional<double> y = call.command.FindDouble("y");
  if (!x || !y) {
    std::move(reply).Run(base::Value(), "hitTest needs x and y");
    return;
  }
  const gfx::Point point(*x, *y);
  const views::View* handler =
      call.widget->GetRootView()->GetEventHandlerForPoint(point);
  const PressTarget target = PressTargetAt(call.widget, point);
  // The platform's own search for who takes a press there, as each mouse
  // event runs it: the median of a hundred and one, in microseconds.
  std::vector<base::TimeDelta> times;
  for (int i = 0; i < 101; ++i) {
    const base::TimeTicks start = base::TimeTicks::Now();
    PressTargetAt(call.widget, point);
    times.push_back(base::TimeTicks::Now() - start);
  }
  std::nth_element(times.begin(), times.begin() + 50, times.end());
  std::move(reply).Run(
      base::Value(
          base::DictValue()
              .Set("handler", handler ? handler->GetClassName() : std::string())
              // The view by its id, or the nearest one around it:
              // what a scenario names.
              .Set("view", std::string(NearestViewIdName(handler)))
              .Set("platform", PlatformViewAt(call.widget, point))
              .Set("target", target == PressTarget::kPage    ? "page"
                             : target == PressTarget::kViews ? "views"
                             : target == PressTarget::kDrag  ? "drag"
                                                             : "other")
              .Set("micros", times[50].InMicrosecondsF())),
      std::string());
}

// A trackpad scroll at a point of the window (from its top left), by `dy`
// points: negative moves the content up, as a swipe up does.
// static
void Automation::Commands::ScrollWindow(const Call& call, Reply reply) {
  const std::optional<double> x = call.command.FindDouble("x");
  const std::optional<double> y = call.command.FindDouble("y");
  const std::optional<double> dy = call.command.FindDouble("dy");
  if (!x || !y || !dy) {
    std::move(reply).Run(base::Value(), "scrollWindow needs x, y, and dy");
    return;
  }
  PostScroll(call.widget, *x, *y, *dy);
  std::move(reply).Run(base::Value(), std::string());
}

// A drag from one point of the window to another, { from: [x, y], to:
// [x, y], steps }, through Views' own dispatch (Drag): each step timed from
// its event to the window's layout, and the page's width after it.
// static
void Automation::Commands::DragWindow(const Call& call, Reply reply) {
  const base::ListValue* from = call.command.FindList("from");
  const base::ListValue* to = call.command.FindList("to");
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
  for (double ms : Drag(call.widget, *start, *end,
                        call.command.FindInt("steps").value_or(10))) {
    steps.Append(ms);
  }
  std::move(reply).Run(
      base::Value(base::DictValue()
                      .Set("stepMs", std::move(steps))
                      .Set("pageWidth", call.window->PageBounds().width())),
      std::string());
}

// One phase of a drag at a point of the window (from its top left),
// { phase: "down" | "drag" | "up", x, y }, through Views' own dispatch
// (Pointer), so a scenario paces a drag as a hand does and reads the rows
// between its moves, in the background too: the step's time to the
// window's layout, in milliseconds.
// static
void Automation::Commands::PointerWindow(const Call& call, Reply reply) {
  const std::string* phase = call.command.FindString("phase");
  const std::optional<double> x = call.command.FindDouble("x");
  const std::optional<double> y = call.command.FindDouble("y");
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
  const double ms = Pointer(call.widget, *type, gfx::PointF(*x, *y));
  std::move(reply).Run(base::Value(base::DictValue().Set("ms", ms)),
                       std::string());
}

// Where each of the window's named parts that shows is, by its name
// (view_ids.h: "pane.sidebar.profiles", "pane.tab.<id>",
// "pane.toolbar.back"), from the window's top left: x, y, width, height. A
// row scrolled out of view lies outside the list's.
// static
void Automation::Commands::Frames(const Call& call, Reply reply) {
  const std::string* prefix = call.command.FindString("prefix");
  base::DictValue frames;
  const views::View* root = call.widget->GetRootView();
  std::vector<std::pair<std::string, const views::View*>> named =
      call.window->root()->sidebar()->NamedViews();
  for (auto& entry : call.window->root()->toolbar()->NamedViews()) {
    named.push_back(std::move(entry));
  }
  for (const int id : {kViewIdSadTab, kViewIdSadTabReload}) {
    if (const views::View* view = call.window->root()->GetViewByID(id)) {
      named.emplace_back(std::string(ViewIdName(id)), view);
    }
  }
  for (auto& entry : call.window->root()->settings()->NamedViews()) {
    named.push_back(std::move(entry));
  }
  if (pane::AppIconPicker* picker = call.window->root()->app_icon_picker()) {
    for (auto& entry : picker->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  // Quit Pane?'s buttons, while it asks, and its card as "pane.quit", as
  // the Swift's dialog surface was named.
  if (call.window->root()->quit_dialog_showing()) {
    for (const int id : {kViewIdQuitConfirm, kViewIdQuitCancel}) {
      if (const views::View* view = call.window->root()->GetViewByID(id)) {
        named.emplace_back(std::string(ViewIdName(id)), view);
      }
    }
    const auto* dialog = static_cast<const ModalDialog*>(
        call.window->root()->GetViewByID(kViewIdQuit));
    const std::string_view card_name = ViewIdName(kViewIdQuit);
    if (dialog && (!prefix || card_name.starts_with(*prefix))) {
      const gfx::Rect card =
          views::View::ConvertRectToTarget(dialog, root, dialog->CardBounds());
      frames.Set(card_name, RectList(card));
    }
  }
  // Pane's own alert and About Pane, where Pane draws them (Windows): their
  // content as "pane.alert" and "pane.about", and their buttons.
  if (SystemAlertDialog* alert = call.window->root()->alert()) {
    named.emplace_back(std::string(ViewIdName(kViewIdAlert)), alert);
    named.emplace_back(std::string(ViewIdName(kViewIdAlertCancel)),
                       alert->cancel_button());
    named.emplace_back(std::string(ViewIdName(kViewIdAlertAction)),
                       alert->action_button());
  }
  if (AboutDialog* about = call.window->root()->about()) {
    named.emplace_back(std::string(ViewIdName(kViewIdAbout)), about);
    if (const views::View* ok = about->GetViewByID(kViewIdAboutOk)) {
      named.emplace_back(std::string(ViewIdName(kViewIdAboutOk)), ok);
    }
  }
  // A page's dialog: its card's content as "pane.dialog", its fields, and
  // its buttons.
  if (PageDialog* dialog = call.window->root()->page_dialog()) {
    for (auto& entry : dialog->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  // Find in page's bar, its field, count, and buttons, while it shows,
  // and its card as "pane.find", as the Swift named the bar.
  if (FindBarView* find = call.window->root()->find_bar()) {
    for (auto& entry : find->NamedViews()) {
      named.push_back(std::move(entry));
    }
    const std::string_view card_name = ViewIdName(kViewIdFind);
    if (!prefix || card_name.starts_with(*prefix)) {
      const gfx::Rect card = views::View::ConvertRectToTarget(
          call.window->root(), root, call.window->root()->FindBarBounds());
      frames.Set(card_name, RectList(card));
    }
  }
  // The extensions menu and the more menu, while they show.
  if (ExtensionsMenuView* menu = call.window->root()->extensions_menu()) {
    for (auto& entry : menu->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  if (ExtensionMoreMenuView* more =
          call.window->root()->extension_more_menu()) {
    for (auto& entry : more->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  // An extension's prompt, while it asks: its card by its own name, as
  // the Swift named the surface ("pane.extensionInstall",
  // "pane.extensionPopup"), its buttons, and Report abuse.
  if (ExtensionPromptView* prompt = call.window->root()->extension_prompt()) {
    for (auto& entry : prompt->NamedViews()) {
      if (entry.second != prompt) {
        named.push_back(std::move(entry));
        continue;
      }
      if (!prefix || entry.first.starts_with(*prefix)) {
        const gfx::Rect card = views::View::ConvertRectToTarget(
            call.window->root(), root,
            call.window->root()->ExtensionPromptBounds());
        frames.Set(entry.first, RectList(card));
      }
    }
  }
  // The profile sheet, while it shows: its controls, and its panel as
  // "pane.profileSheet", as the Swift named the sheet.
  if (ProfileSheet* sheet = call.window->root()->profile_sheet()) {
    for (auto& entry : sheet->NamedViews()) {
      if (entry.second != sheet) {
        named.push_back(std::move(entry));
        continue;
      }
      if (!prefix || entry.first.starts_with(*prefix)) {
        const gfx::Rect card = views::View::ConvertRectToTarget(
            call.window->root(), root,
            call.window->root()->ProfileSheetBounds());
        frames.Set(entry.first, RectList(card));
      }
    }
  }
  // The PIN screen, while it shows: its panel, dots, keys, and buttons.
  for (auto& entry : call.window->root()->lock_screen()->NamedViews()) {
    named.push_back(std::move(entry));
  }
  // The page, DevTools docked beside it, and the full screen's hint.
  for (const int id :
       {kViewIdContent, kViewIdDevTools, kViewIdExclusiveAccessHint}) {
    if (const views::View* view = call.window->root()->GetViewByID(id)) {
      named.emplace_back(std::string(ViewIdName(id)), view);
    }
  }
  for (const auto& [view_name, view] : named) {
    if (view->IsDrawn() && (!prefix || view_name.starts_with(*prefix))) {
      frames.Set(view_name, FrameIn(view, root));
    }
  }
  // The switcher's panel and rows, which it draws itself.
  if (TabSwitcherView* switcher = call.window->root()->switcher();
      switcher && switcher->IsDrawn()) {
    for (const auto& [row_name, row] : switcher->NamedFrames()) {
      if (!prefix || row_name.starts_with(*prefix)) {
        const gfx::Rect frame =
            views::View::ConvertRectToTarget(switcher, root, row);
        frames.Set(row_name, RectList(frame));
      }
    }
  }
  std::move(reply).Run(base::Value(std::move(frames)), std::string());
}

// A menu of Settings' (its name, "pane.settings.theme"), as its model holds
// what the Mac's menu shows: its items; with `choose`, the item of that
// title chosen, as a click in the menu chooses it. The menu itself, which
// runs with the app's tasks held, is the front's.
// static
void Automation::Commands::SettingsMenu(const Call& call, Reply reply) {
  const std::string* menu_name = call.command.FindString("menu");
  SettingsView* settings = call.window->root()->settings();
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
  if (const std::string* choose = call.command.FindString("choose")) {
    const std::optional<size_t> chosen =
        ItemTitled(model, base::UTF8ToUTF16(*choose));
    if (!chosen) {
      std::move(reply).Run(base::Value(), "no menu item " + *choose);
      return;
    }
    model.ActivatedAt(*chosen);
  }
  std::move(reply).Run(base::Value(std::move(items)), std::string());
}

// A key through Views' own dispatch, { key, modifiers, phase }, as the
// platform hands the window a key: the window's accelerators, then the
// focused view, in the background too, where `key` (AppKit's path) needs
// the front. `phase` is "press" or "release" for one half alone ("control"
// let go, with no modifier left, is Ctrl's release); both by default.
// static
void Automation::Commands::DispatchKey(const Call& call, Reply reply) {
  const std::string* key = call.command.FindString("key");
  const std::optional<ui::KeyboardCode> code =
      key ? KeyCodeOf(*key) : std::nullopt;
  if (!code) {
    std::move(reply).Run(base::Value(), "no key code for that key");
    return;
  }
  FocusAsKeyWindow(call.widget);
  const int flags = FlagsOf(Modifiers(call.command));
  const std::string* phase = call.command.FindString("phase");
  std::vector<ui::EventType> types;
  if (!phase || *phase != "release") {
    types.push_back(ui::EventType::kKeyPressed);
  }
  if (!phase || *phase != "press") {
    types.push_back(ui::EventType::kKeyReleased);
  }
  for (const ui::EventType type : types) {
    ui::KeyEvent event(type, *code, flags);
    call.widget->OnKeyEvent(&event);
  }
  std::move(reply).Run(base::Value(), std::string());
}

// A row's right-click menu, as its model holds what the Mac's menu shows:
// { profile } or { tab }, its items; with `choose`, the item of that title
// chosen, as a click in the menu chooses it, after the menu has gone. The
// menu itself, which runs with the app's tasks held, is the front's.
// static
void Automation::Commands::RowMenu(const Call& call, Reply reply) {
  const std::string* profile_id = call.command.FindString("profile");
  const std::string* tab_id = call.command.FindString("tab");
  SidebarView* sidebar = call.window->root()->sidebar();
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
  if (const std::string* choose = call.command.FindString("choose")) {
    const std::optional<size_t> chosen =
        ItemTitled(*menu, base::UTF8ToUTF16(*choose));
    if (!chosen || !menu->IsEnabledAt(*chosen)) {
      std::move(reply).Run(base::Value(), "no menu item " + *choose);
      return;
    }
    menu->ActivatedAt(*chosen);
  }
  std::move(reply).Run(base::Value(std::move(items)), std::string());
}

// A button of the page's dialog showing, by its title, { title, texts }:
// its fields take `texts` first, in order, as a value set through
// accessibility does, and the button takes a real click at its middle.
// static
void Automation::Commands::ClickSheetButton(const Call& call, Reply reply) {
  const std::string* title = call.command.FindString("title");
  // An extension's prompt first, over a page's dialog: its accept or its
  // Cancel.
  if (const ExtensionPrompt* prompt = call.app.Extensions().prompt();
      prompt && title) {
    ExtensionPromptView* view =
        call.window->root() ? call.window->root()->extension_prompt() : nullptr;
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
    ClickMiddle(call.widget, button);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }
  PageDialog* dialog =
      call.window->root() ? call.window->root()->page_dialog() : nullptr;
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
  if (const base::ListValue* texts = call.command.FindList("texts")) {
    std::vector<std::u16string> fields;
    for (const base::Value& text : *texts) {
      fields.push_back(text.is_string() ? base::UTF8ToUTF16(text.GetString())
                                        : std::u16string());
    }
    dialog->SetFieldTexts(fields);
  }
  const views::View* button = dialog->button(found - buttons.begin());
  ClickMiddle(call.widget, button);
  std::move(reply).Run(base::Value(), std::string());
}

// The page's dialog's fields given `texts`, in order, as a value set
// through accessibility gives them (VoiceOver, Voice Control), with no
// button pressed: { texts }.
// static
void Automation::Commands::FillSheet(const Call& call, Reply reply) {
  PageDialog* dialog =
      call.window->root() ? call.window->root()->page_dialog() : nullptr;
  const base::ListValue* texts = call.command.FindList("texts");
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
}

// What assistive technologies read of the page's dialog showing (ADR
// 0071), from Views' own accessibility data, as VoiceOver gets it: the
// dialog's role, name, and modality, and each field's and button's name,
// with the default button marked.
// static
void Automation::Commands::DialogAccessibility(const Call& call, Reply reply) {
  PageDialog* dialog =
      call.window->root() ? call.window->root()->page_dialog() : nullptr;
  if (!dialog) {
    std::move(reply).Run(base::Value(), "no dialog");
    return;
  }
  const auto describe = [](const views::View* view) {
    ui::AXNodeData data;
    view->GetViewAccessibility().GetAccessibleNodeData(&data);
    return base::DictValue()
        .Set("role", ui::ToString(data.role))
        .Set("name", data.GetStringAttribute(ax::mojom::StringAttribute::kName))
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
}

// The system's file panel showing answered as its own buttons would,
// { paths } chosen or { cancel: true }, and taken down: a panel run out of
// process takes no choice from here (file_panels.h).
// static
void Automation::Commands::AnswerFileChooser(const Call& call, Reply reply) {
  std::optional<std::vector<base::FilePath>> paths;
  if (!call.command.FindBool("cancel").value_or(false)) {
    paths.emplace();
    if (const base::ListValue* chosen = call.command.FindList("paths")) {
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
}

// A key typed into the selected page, { key }: the page takes the
// keyboard, and the window hands it the key as the user's (input a hung
// page leaves unanswered), in the background too.
// static
void Automation::Commands::TypeInPage(const Call& call, Reply reply) {
  const std::string* key = call.command.FindString("key");
  if (!call.window->selected() || !call.window->root()) {
    std::move(reply).Run(base::Value(), "no page");
    return;
  }
  call.window->root()->FocusPage();
  if (!key || !SendKeyToWindow(call.widget, *key, Modifiers(call.command))) {
    std::move(reply).Run(base::Value(), "no key code for that key");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// A key, with { modifiers }, typed into whatever has the window's keys, as
// the window hands it over, in the background too: the page, or a view of
// Pane's (the address being edited), its shortcuts' paths and all.
// static
void Automation::Commands::WindowKey(const Call& call, Reply reply) {
  const std::string* key = call.command.FindString("key");
  FocusAsKeyWindow(call.widget);
  if (!key || !SendKeyToWindow(call.widget, *key, Modifiers(call.command))) {
    std::move(reply).Run(base::Value(), "no key code for that key");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// Installs from the Chrome Web Store by its id into a profile, opened in
// the engine first if needed, as the store's own button does, Pane's
// dialog asking first: { id, profile }. The reply comes once the install
// has begun.
// static
void Automation::Commands::InstallExtension(const Call& call, Reply reply) {
  const std::string* id = call.command.FindString("id");
  const std::string* profile_id = call.command.FindString("profile");
  if (!id || !profile_id || !crx_file::id_util::IdIsValid(*id) ||
      !call.app.model().FindProfile(*profile_id)) {
    std::move(reply).Run(
        base::Value(),
        "installExtension needs an extension's id and a profile");
    return;
  }
  PANE_TRACE("call", "PaneBrowserContext.installExtension",
             base::DictValue().Set("extension", *id));
  call.app.WithProfile(
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
                           extensions::webstore_install::Result result) {},
                        id));
            installer->BeginInstall();
            std::move(reply).Run(base::Value(), std::string());
          },
          *id,
          call.widget ? call.widget->GetWeakPtr()
                      : base::WeakPtr<views::Widget>(),
          std::move(reply)));
}

// Chrome asking to add an extension, as a store's install asks before it
// adds one, with no store to reach: Chrome's own prompt for one loaded
// already, { id, profile }, which shows Pane's dialog (AskToAddExtension)
// over the window. Its answer is traced (PaneExtensionInstallRequest), and
// adds nothing.
// static
void Automation::Commands::AskToAddExtension(const Call& call, Reply reply) {
  const std::string* id = call.command.FindString("id");
  const std::string* profile_id = call.command.FindString("profile");
  if (!id || !profile_id || !call.app.model().FindProfile(*profile_id)) {
    std::move(reply).Run(
        base::Value(),
        "askToAddExtension needs an extension's id and a profile");
    return;
  }
  call.app.WithProfile(
      *profile_id,
      base::BindOnce(
          [](base::WeakPtr<Automation> self, std::string id,
             base::WeakPtr<views::Widget> window, Reply reply,
             Profile* profile) {
            const extensions::Extension* extension =
                profile ? extensions::ExtensionRegistry::Get(profile)
                              ->enabled_extensions()
                              .GetByID(id)
                        : nullptr;
            if (!self || !extension) {
              std::move(reply).Run(base::Value(),
                                   "askToAddExtension: no such extension "
                                   "on in the profile");
              return;
            }
            // Kept until the next ask: it loads the icon before it asks.
            self->asked_to_add_ = std::make_unique<ExtensionInstallPrompt>(
                profile,
                window ? window->GetNativeWindow() : gfx::NativeWindow(),
                std::make_unique<extensions::InstallPromptData>(
                    extensions::InstallPromptData::INSTALL_PROMPT));
            self->prompt_answer_.reset();
            // Its answer, for promptAnswer.
            self->asked_to_add_->ShowDialog(
                base::BindOnce(
                    [](base::WeakPtr<Automation> self,
                       ExtensionInstallPrompt::DoneCallbackPayload payload) {
                      if (!self) {
                        return;
                      }
                      using Result =
                          extensions::ExtensionInstallPromptClient::Result;
                      self->prompt_answer_ =
                          payload.result == Result::USER_CANCELED ? "cancelled"
                          : payload.result == Result::ABORTED     ? "aborted"
                                                                  : "accepted";
                    },
                    self),
                extension, /*icon=*/nullptr,
                ExtensionInstallPrompt::GetDefaultShowDialogCallback());
            std::move(reply).Run(base::Value(), std::string());
          },
          call.channel.weak_factory_.GetWeakPtr(), *id,
          call.widget ? call.widget->GetWeakPtr()
                      : base::WeakPtr<views::Widget>(),
          std::move(reply)));
}

// How the prompt askToAddExtension showed was answered: "accepted",
// "cancelled", or "aborted"; null while it asks, or before any.
// static
void Automation::Commands::PromptAnswer(const Call& call, Reply reply) {
  std::move(reply).Run(call.channel.prompt_answer_
                           ? base::Value(*call.channel.prompt_answer_)
                           : base::Value(),
                       std::string());
}

// Where Chrome's own tab-modal dialogs of the selected tab show, asked of
// its Browser as Chrome asks it, for one { width, height } big: the
// dialog's place, the most it may take, and where the page is, each from
// the window's top left.
// static
void Automation::Commands::ModalHost(const Call& call, Reply reply) {
  content::WebContents* page = call.window->selected();
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
  const gfx::Size size(call.command.FindInt("width").value_or(260),
                       call.command.FindInt("height").value_or(100));
  const gfx::Point at = host->GetDialogPosition(size);
  const gfx::Size most = host->GetMaximumDialogSize();
  const gfx::Rect bounds = call.window->PageBounds();
  std::move(reply).Run(
      base::Value(
          base::DictValue()
              .Set("position", base::ListValue().Append(at.x()).Append(at.y()))
              .Set("maximum",
                   base::ListValue().Append(most.width()).Append(most.height()))
              .Set("page", RectList(bounds))
              .Set("hostView", static_cast<bool>(host->GetHostView()))
              .Set("activates", host->ShouldActivateDialog())),
      std::string());
}

// A button of the alert the window shows as a sheet, or of the app-modal
// panel (the system's print panel), { title }, pressed as a click on it
// does.
// static
void Automation::Commands::ClickModalButton(const Call& call, Reply reply) {
  const std::string* title = call.command.FindString("title");
  // Pane's own alert, where Pane draws the system's (Windows).
  if (SystemAlertDialog* alert =
          call.window->root() ? call.window->root()->alert() : nullptr;
      alert && title) {
    for (views::View* button :
         {alert->cancel_button(), alert->action_button()}) {
      if (base::UTF16ToUTF8(button->GetViewAccessibility().GetCachedName()) ==
          *title) {
        ClickMiddle(call.widget, button);
        std::move(reply).Run(base::Value(), std::string());
        return;
      }
    }
  }
  if (!title || !PressSheetButton(call.widget, *title)) {
    std::move(reply).Run(base::Value(),
                         "no button " + (title ? *title : std::string()) +
                             " in the window's sheet or modal panel");
    return;
  }
  std::move(reply).Run(base::Value(), std::string());
}

// The window at a new size, as one step of a live resize sets it: how long
// the step took, in milliseconds, and where the page went.
// static
void Automation::Commands::ResizeWindow(const Call& call, Reply reply) {
  const std::optional<int> width = call.command.FindInt("width");
  const std::optional<int> height = call.command.FindInt("height");
  if (!width || !height) {
    std::move(reply).Run(base::Value(), "resizeWindow needs width and height");
    return;
  }
  gfx::Rect bounds = call.widget->GetWindowBoundsInScreen();
  bounds.set_size(gfx::Size(*width, *height));
  // Where on the screen, when asked: { x, y } from its work area's top
  // left, or y "low", the window's top two thirds of the way down, where
  // the screen leaves more room above the toolbar than under it.
  const gfx::Rect work =
      display::Screen::Get()
          ->GetDisplayNearestWindow(call.widget->GetNativeWindow())
          .work_area();
  if (const std::optional<int> x = call.command.FindInt("x")) {
    bounds.set_x(work.x() + *x);
  }
  if (const std::string* low = call.command.FindString("y");
      low && *low == "low") {
    bounds.set_y(work.y() + work.height() * 2 / 3);
  } else if (const std::optional<int> y = call.command.FindInt("y")) {
    bounds.set_y(work.y() + *y);
  }
  const base::TimeTicks start = base::TimeTicks::Now();
  call.widget->SetBounds(bounds);
  const base::TimeDelta took = base::TimeTicks::Now() - start;
  const gfx::Rect page = call.window->PageBounds();
  const gfx::Rect client = call.widget->GetClientAreaBoundsInScreen();
  std::move(reply).Run(
      base::Value(
          base::DictValue()
              .Set("ms", took.InMillisecondsF())
              .Set("window", base::ListValue()
                                 .Append(client.width())
                                 .Append(client.height()))
              .Set("screen", RectList(work))
              .Set("frame",
                   base::ListValue().Append(client.x()).Append(client.y()))
              .Set("page", RectList(page))),
      std::string());
}

// Where the page's card is, in the window's points from its top left, and
// the corners its layer rounds (top left, top right, bottom right, bottom
// left), which a render leaves out with the page.
// static
void Automation::Commands::PageLayout(const Call& call, Reply reply) {
  const gfx::Rect page = call.window->PageBounds();
  const gfx::RoundedCornersF corners = call.window->root()->PageCorners();
  std::move(reply).Run(
      base::Value(base::DictValue()
                      .Set("page", RectList(page))
                      .Set("corners", base::ListValue()
                                          .Append(corners.upper_left())
                                          .Append(corners.upper_right())
                                          .Append(corners.lower_right())
                                          .Append(corners.lower_left()))),
      std::string());
}

// The profile sheet, as a user opens it, { profile? }: New, by a real
// click on the sidebar's New; Edit, as the profile's row's menu chooses
// Edit profile (rowMenu's). Closed by a real click on its Cancel.
// static
void Automation::Commands::OpenOrCloseProfileSheet(const Call& call,
                                                   Reply reply) {
  RootView* root = call.window->root();
  if (call.name == "closeProfileSheet") {
    ProfileSheet* sheet = root->profile_sheet();
    const views::View* cancel =
        sheet ? sheet->GetViewByID(kViewIdProfileCancel) : nullptr;
    if (!cancel) {
      std::move(reply).Run(base::Value(), std::string());
      return;
    }
    // Pressed where it rests: a sheet still sliding in is pressed once it
    // stops, as a hand reaches the button then.
    root->WhenProfileSheetRests(base::BindOnce(
        &ClickMiddle, base::Unretained(call.widget), base::Unretained(cancel)));
    std::move(reply).Run(base::Value(), std::string());
    return;
  }
  if (const std::string* profile_id = call.command.FindString("profile")) {
    ProfileRow* row = root->sidebar()->FindProfileRow(*profile_id);
    if (!row) {
      std::move(reply).Run(base::Value(),
                           "openProfileSheet needs a row the sidebar shows");
      return;
    }
    ui::SimpleMenuModel& menu = row->menu_model();
    if (const std::optional<size_t> edit = ItemTitled(menu, u"Edit profile")) {
      menu.ActivatedAt(*edit);
    }
    std::move(reply).Run(base::Value(), std::string());
    return;
  }
  const views::View* add = root->GetViewByID(kViewIdSidebarNew);
  if (!add || !add->IsDrawn()) {
    std::move(reply).Run(base::Value(), "the sidebar shows no New");
    return;
  }
  ClickMiddle(call.widget, add);
  std::move(reply).Run(base::Value(), std::string());
}

// A field of the profile sheet given `text`, { field: "name" | "proxyHost"
// | "proxyPort" | "proxyUsername" | "proxyPassword", text }, as a value
// set through accessibility gives it (VoiceOver, Voice Control): the field
// takes the keys, then its whole text is `text`, a paste's way.
// static
void Automation::Commands::SheetField(const Call& call, Reply reply) {
  ProfileSheet* sheet = call.window->root()->profile_sheet();
  const std::string* field_name = call.command.FindString("field");
  const std::string* text = call.command.FindString("text");
  views::Textfield* field =
      sheet && field_name ? sheet->FindField(*field_name) : nullptr;
  if (!field || !text || !field->IsDrawn()) {
    std::move(reply).Run(base::Value(),
                         "sheetField needs a field the sheet shows, and text");
    return;
  }
  ui::AXActionData focus;
  focus.action = ax::mojom::Action::kFocus;
  field->HandleAccessibleAction(focus);
  ui::AXActionData value;
  value.action = ax::mojom::Action::kSetValue;
  value.value = *text;
  field->HandleAccessibleAction(value);
  std::move(reply).Run(base::Value(), std::string());
}

// A menu of the profile sheet's, { menu: "timeZone" | "proxyType", choose?:
// [titles] }, as its model holds what the Mac's menu shows: its items, a
// submenu's under its own; with `choose`, the item at that path chosen, as
// a click in the menu chooses it. The menu itself, which runs with the
// app's tasks held, is the front's.
// static
void Automation::Commands::SheetMenu(const Call& call, Reply reply) {
  ProfileSheet* sheet = call.window->root()->profile_sheet();
  const std::string* menu_name = call.command.FindString("menu");
  MenuButton* menu = sheet && menu_name ? sheet->FindMenu(*menu_name) : nullptr;
  if (!menu || !menu->IsDrawn()) {
    std::move(reply).Run(base::Value(),
                         "sheetMenu needs a menu the sheet shows");
    return;
  }
  const auto describe = [](const ui::MenuModel& model,
                           auto& self) -> base::ListValue {
    base::ListValue items;
    for (size_t index = 0; index < model.GetItemCount(); ++index) {
      if (model.GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR) {
        items.Append(base::DictValue().Set("title", "-"));
        continue;
      }
      base::DictValue item = base::DictValue()
                                 .Set("title", model.GetLabelAt(index))
                                 .Set("checked", model.IsItemCheckedAt(index));
      if (const ui::MenuModel* submenu = model.GetSubmenuModelAt(index)) {
        item.Set("submenu", self(*submenu, self));
      }
      items.Append(std::move(item));
    }
    return items;
  };
  base::ListValue items = describe(menu->menu_model(), describe);
  if (const base::ListValue* path = call.command.FindList("choose")) {
    ui::MenuModel* model = &menu->menu_model();
    std::optional<size_t> chosen;
    for (size_t step = 0; step < path->size() && model; ++step) {
      const base::Value& title = (*path)[step];
      chosen = title.is_string()
                   ? ItemTitled(*model, base::UTF8ToUTF16(title.GetString()))
                   : std::nullopt;
      if (!chosen) {
        std::move(reply).Run(
            base::Value(),
            "no menu item " +
                (title.is_string() ? title.GetString() : std::string()));
        return;
      }
      if (step + 1 < path->size()) {
        model = model->GetSubmenuModelAt(*chosen);
      }
    }
    if (!model || !chosen) {
      std::move(reply).Run(base::Value(), "sheetMenu needs a path to choose");
      return;
    }
    model->ActivatedAt(*chosen);
  }
  std::move(reply).Run(base::Value(std::move(items)), std::string());
}

// What assistive technologies read of the window's controls (ADR 0071),
// from Views' own accessibility data, as VoiceOver gets it, { prefix? }:
// the sidebar's, the toolbar's, Settings', and the profile sheet's, by
// their names ("pane.profile.name"), each with its role, its name to the
// screen reader, its value, its description (a problem's words, a
// switch's note), whether it is checked or chosen, and whether it is
// modal.
// static
void Automation::Commands::Accessibility(const Call& call, Reply reply) {
  const std::string* prefix = call.command.FindString("prefix");
  RootView* root = call.window->root();
  std::vector<std::pair<std::string, const views::View*>> named =
      root->sidebar()->NamedViews();
  for (auto& entry : root->toolbar()->NamedViews()) {
    named.push_back(std::move(entry));
  }
  for (auto& entry : root->settings()->NamedViews()) {
    named.push_back(std::move(entry));
  }
  if (ProfileSheet* sheet = root->profile_sheet()) {
    for (auto& entry : sheet->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  // Pane's own alert and About Pane, where Pane draws them: their dialogs,
  // as a screen reader meets them, and their buttons.
  for (const int id : {kViewIdAlert, kViewIdAbout}) {
    const views::View* content = id == kViewIdAlert
                                     ? static_cast<views::View*>(root->alert())
                                     : root->about();
    if (!content) {
      continue;
    }
    for (const views::View* view = content; view; view = view->parent()) {
      if (view->GetID() == id) {
        named.emplace_back(std::string(ViewIdName(id)), view);
        break;
      }
    }
  }
  if (SystemAlertDialog* alert = root->alert()) {
    named.emplace_back(std::string(ViewIdName(kViewIdAlertCancel)),
                       alert->cancel_button());
    named.emplace_back(std::string(ViewIdName(kViewIdAlertAction)),
                       alert->action_button());
  }
  if (AboutDialog* about = root->about()) {
    if (const views::View* ok = about->GetViewByID(kViewIdAboutOk)) {
      named.emplace_back(std::string(ViewIdName(kViewIdAboutOk)), ok);
    }
  }
  base::DictValue controls;
  for (const auto& [control_name, view] : named) {
    if (!view->IsDrawn() || (prefix && !control_name.starts_with(*prefix))) {
      continue;
    }
    ui::AXNodeData data;
    view->GetViewAccessibility().GetAccessibleNodeData(&data);
    controls.Set(
        control_name,
        base::DictValue()
            .Set("role", ui::ToString(data.role))
            .Set("name",
                 data.GetStringAttribute(ax::mojom::StringAttribute::kName))
            .Set("value",
                 data.GetStringAttribute(ax::mojom::StringAttribute::kValue))
            .Set("description", data.GetStringAttribute(
                                    ax::mojom::StringAttribute::kDescription))
            .Set("checked",
                 data.GetCheckedState() == ax::mojom::CheckedState::kTrue)
            .Set("selected",
                 data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected))
            .Set("modal",
                 data.GetBoolAttribute(ax::mojom::BoolAttribute::kModal)));
  }
  std::move(reply).Run(base::Value(std::move(controls)), std::string());
}

// static
void Automation::Commands::ExtensionsMenu(const Call& call, Reply reply) {
  const std::string* control = call.command.FindString("control");
  const std::string* id = call.command.FindString("id");
  if (!control) {
    std::move(reply).Run(base::Value(), "extensionsMenu needs a control");
    return;
  }
  if (*control == "viewWebPermissions") {
    ActionMenu* menu = id ? call.app.Extensions().MakeActionMenu(*id) : nullptr;
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
      {"remove", "pane.extensionMoreMenu.remove"},
      {"manageExtension", "pane.extensionMoreMenu.manage"},
  };
  const auto found = names.find(*control);
  std::vector<std::pair<std::string, const views::View*>> named =
      call.window->root()->toolbar()->NamedViews();
  if (ExtensionsMenuView* menu = call.window->root()->extensions_menu()) {
    for (auto& entry : menu->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  if (ExtensionMoreMenuView* more =
          call.window->root()->extension_more_menu()) {
    for (auto& entry : more->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  const views::View* view = nullptr;
  for (const auto& [view_name, each] : named) {
    if (found != names.end() && view_name == found->second && each->IsDrawn()) {
      view = each;
    }
  }
  if (!view) {
    std::move(reply).Run(base::Value(),
                         "extensionsMenu: no " + *control + " shows");
    return;
  }
  ClickMiddle(call.widget, view);
  std::move(reply).Run(base::Value(), std::string());
}

// The shown profile's extension's action, as its button runs it: its
// popup, or chrome.action.onClicked (the Swift's runAction).
// static
void Automation::Commands::RunExtensionAction(const Call& call, Reply reply) {
  const std::string* id = call.command.FindString("id");
  if (!id) {
    std::move(reply).Run(base::Value(), "runExtensionAction needs an id");
    return;
  }
  call.app.Extensions().RunAction(*id);
  std::move(reply).Run(base::Value(), std::string());
}

// static
void Automation::Commands::CloseExtensionPopup(const Call& call, Reply reply) {
  call.app.Extensions().ClosePopup();
  std::move(reply).Run(base::Value(), std::string());
}

// A shown extension's pinned action's right-click menu, made as a
// right-click makes it: the extension's own items and its site access, and
// the whole menu; with `run`, the
// item of that title chosen, as a click in the system's menu chooses it.
// static
void Automation::Commands::ExtensionActionMenu(const Call& call, Reply reply) {
  const std::string* id = call.command.FindString("id");
  ActionMenu* menu = id ? call.app.Extensions().MakeActionMenu(*id) : nullptr;
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
  if (const std::string* run = call.command.FindString("run")) {
    if (!menu->Choose(base::UTF8ToUTF16(*run))) {
      std::move(reply).Run(base::Value(), "no action menu item " + *run);
      return;
    }
  }
  std::move(reply).Run(base::Value(std::move(described)), std::string());
}

// An extension's pin, its being on, and its removal, in a profile, set in
// the engine: { id, profile, pinned | enabled }.
// static
void Automation::Commands::PinEnableOrUninstallExtension(const Call& call,
                                                         Reply reply) {
  const std::string* id = call.command.FindString("id");
  const std::string* profile_id = call.command.FindString("profile");
  Profile* profile = profile_id ? call.app.ProfileIfOpen(*profile_id) : nullptr;
  if (!id || !profile) {
    std::move(reply).Run(base::Value(),
                         call.name + " needs an id and an open profile");
    return;
  }
  if (call.name == "setExtensionPinned") {
    call.app.Extensions().SetPinned(
        *profile, *id, call.command.FindBool("pinned").value_or(true));
  } else if (call.name == "setExtensionEnabled") {
    auto* registrar = extensions::ExtensionRegistrar::Get(profile);
    if (call.command.FindBool("enabled").value_or(true)) {
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
}

// Saved, then closed, then the volume detached: the reply comes once the
// save is on the disk.
// static
void Automation::Commands::Quit(const Call& call, Reply reply) {
  call.app.Quit(base::BindOnce(
      [](Reply reply) { std::move(reply).Run(base::Value(), std::string()); },
      std::move(reply)));
}

void Automation::OpenPinScreen(model::PinEntry::Mode mode,
                               base::TimeTicks started,
                               int step,
                               Reply reply) {
  Window* window = app_->window();
  views::Widget* widget = window ? window->widget() : nullptr;
  RootView* root = window ? window->root() : nullptr;
  if (!widget || !root) {
    std::move(reply).Run(base::Value(), "showPinScreen: the window is closed");
    return;
  }
  if (const std::optional<model::PinEntry>& entry = AppData::Get()->PinScreen();
      entry && entry->mode() == mode) {
    std::move(reply).Run(base::Value(), std::string());
    return;
  }
  if (base::TimeTicks::Now() - started > base::Seconds(5)) {
    std::move(reply).Run(base::Value(),
                         "showPinScreen: the PIN screen did not open");
    return;
  }
  const int button_id =
      mode == model::PinEntry::Mode::kSetup    ? kViewIdSettingsSetUpPin
      : mode == model::PinEntry::Mode::kChange ? kViewIdSettingsChangePin
                                               : kViewIdSettingsRemovePin;
  SettingsView* settings = root->settings();
  views::View* button = settings->GetViewByID(button_id);
  int next = step;
  if (step == 0) {
    // Settings, as the sidebar's own button shows it.
    if (!settings->IsDrawn()) {
      const views::View* open = root->GetViewByID(kViewIdSidebarSettings);
      if (!open || !open->IsDrawn()) {
        std::move(reply).Run(base::Value(),
                             "showPinScreen: the sidebar shows no Settings");
        return;
      }
      ClickMiddle(widget, open);
    }
    next = 1;
  } else if (step == 1 && settings->IsDrawn()) {
    // The button where a user scrolls to it.
    if (!button || !button->IsDrawn()) {
      std::move(reply).Run(base::Value(),
                           "showPinScreen: Settings shows no such button");
      return;
    }
    button->ScrollViewToVisible();
    next = 2;
  } else if (step == 2) {
    if (!button || !button->IsDrawn()) {
      std::move(reply).Run(base::Value(),
                           "showPinScreen: Settings shows no such button");
      return;
    }
    ClickMiddle(widget, button);
    next = 3;
  }
  // A frame on, what the step changed is laid out where it shows.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Automation::OpenPinScreen, weak_factory_.GetWeakPtr(),
                     mode, started, next, std::move(reply)),
      base::Milliseconds(16));
}

}  // namespace pane
