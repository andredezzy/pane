// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/automation.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path_watcher.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "content/public/browser/web_contents.h"
#include "pane/automation/input.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/render.h"
#include "pane/ui/tabs/tab_id.h"
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
                 reply.Set("state", automation->app_->Snapshot());
                 automation->files_.AsyncCall(&Files::Write)
                     .WithArgs(name, base::WriteJson(reply).value_or("{}"));
               },
               weak_factory_.GetWeakPtr(), std::move(name)));
}

void Automation::Dispatch(const std::string& name,
                          const base::DictValue& command,
                          Reply reply) {
  Window* window = app_->window();

  if (name == "state") {
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  if (name == "createProfile") {
    const std::string* profile_name = command.FindString("name");
    app_->CreateProfile(
        profile_name ? *profile_name : std::string(),
        base::BindOnce(
            [](Reply reply, std::string id) {
              if (id.empty()) {
                std::move(reply).Run(base::Value(),
                                     "the profile could not be opened");
              } else {
                std::move(reply).Run(base::Value(id), std::string());
              }
            },
            std::move(reply)));
    return;
  }

  if (name == "openTab") {
    const std::string* profile_id = command.FindString("profile");
    const std::string* url = command.FindString("url");
    Profile* profile = profile_id ? app_->ProfileWithId(*profile_id) : nullptr;
    if (!profile) {
      std::move(reply).Run(base::Value(), "openTab needs a profile");
      return;
    }
    content::WebContents* contents = window->OpenTab(
        profile, GURL(url ? *url : "about:blank"), /*foreground=*/true);
    if (!contents) {
      std::move(reply).Run(base::Value(), "the tab could not be opened");
      return;
    }
    std::move(reply).Run(base::Value(TabIdOf(contents)), std::string());
    return;
  }

  if (name == "select") {
    const std::string* tab = command.FindString("tab");
    content::WebContents* contents = tab ? TabWithId(*tab) : nullptr;
    if (!contents) {
      std::move(reply).Run(base::Value(), "no such tab");
      return;
    }
    window->Select(contents);
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
      window->NewTab();
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
    views::Widget* widget = window->host().widget();
    std::optional<std::vector<uint8_t>> png =
        path && widget
            ? RenderView(widget->GetRootView(),
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
    if (!key || !PostKey(window->host().widget(), *key, modifiers)) {
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
    PostClick(window->host().widget(), *x, *y,
              command.FindInt("clicks").value_or(1));
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
    PostModifiers(window->host().widget(), modifiers);
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  // What a press at a point of the window (from its top left) would reach:
  // the view Views would hand it, and whether the page or Views takes it
  // (frame.h). A question, not input: it runs without the front.
  if (name == "hitTest") {
    const std::optional<double> x = command.FindDouble("x");
    const std::optional<double> y = command.FindDouble("y");
    views::Widget* widget = window->host().widget();
    if (!x || !y || !widget) {
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
                        .Set("target", target == PressTarget::kPage ? "page"
                                       : target == PressTarget::kViews
                                           ? "views"
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
    views::Widget* widget = window->host().widget();
    if (!width || !height || !widget) {
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

  // The prototype's sheet stands in for the profile sheet (G4).
  if (name == "openProfileSheet" || name == "closeProfileSheet") {
    window->root().ShowSheet(name == "openProfileSheet");
    std::move(reply).Run(base::Value(), std::string());
    return;
  }

  if (name == "quit") {
    std::move(reply).Run(base::Value(), std::string());
    chrome::AttemptUserExit();
    return;
  }

  std::move(reply).Run(base::Value(), "unknown command " + name);
}

}  // namespace pane
