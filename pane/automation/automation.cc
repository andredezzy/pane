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
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/render.h"
#include "pane/ui/tabs/tab_id.h"
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
      : in_(directory.Append("in")),
        out_(directory.Append("out")),
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
    const base::FilePath reply = out_.Append(name);
    const base::FilePath temporary = reply.AddExtension("tmp");
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
      received_.Run(file.BaseName().value(),
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
        .WithArgs(base::FilePath(*path), std::move(*png))
        .Then(base::BindOnce(
            [](Reply reply) {
              std::move(reply).Run(base::Value(), std::string());
            },
            std::move(reply)));
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
