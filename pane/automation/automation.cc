// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/automation.h"

#include <algorithm>
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
#include "pane/app/data_launch.h"
#include "pane/automation/input.h"
#include "pane/automation/windows.h"
#include "pane/model/app_settings.h"
#include "pane/model/model_store.h"
#include "pane/model/pin_entry.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/render.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "pane/ui/window/window.h"
#include "ui/compositor/compositor.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace pane {

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
                 state.Set("nativeWindows",
                           NativeWindows(automation->app_->window()->widget()));
                 reply.Set("state", std::move(state));
                 automation->files_.AsyncCall(&Files::Write)
                     .WithArgs(name, base::WriteJson(reply).value_or("{}"));
               },
               weak_factory_.GetWeakPtr(), std::move(name)));
}

void Automation::Dispatch(const std::string& name,
                          const base::DictValue& command,
                          Reply reply) {
  Window* window = app_->window();

  // An edit of the profiles before they load, or behind the lock, would edit
  // a model that holds none of them, and the load would take it back.
  if ((name == "createProfile" || name == "openTab" || name == "moveTab" ||
       name == "moveProfile") &&
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
       name == "openProfileSheet" || name == "closeProfileSheet")) {
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
    if (name == "moveTab") {
      app_->model().MoveTab(*profile_id, *from, *to);
    } else {
      app_->model().MoveProfile(*from, *to);
    }
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
    if (items == std::vector<std::string>{"File", "New Tab"}) {
      app_->NewTab();
    } else if (items ==
               std::vector<std::string>{"Navigate", "Focus Address Bar"}) {
      window->FocusAddress();
    } else {
      std::move(reply).Run(base::Value(), "no such menu item");
      return;
    }
    std::move(reply).Run(base::Value(), std::string());
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

  // The prototype's sheet stands in for the profile sheet (G4).
  if (name == "openProfileSheet" || name == "closeProfileSheet") {
    window->root()->ShowSheet(name == "openProfileSheet");
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
