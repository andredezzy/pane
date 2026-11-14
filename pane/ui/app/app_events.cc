// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/chrome_process_singleton.h"
#include "pane/browser/engine_trace.h"
#include "pane/model/test_hooks.h"
#include "pane/ui/app/handed_pages.h"

namespace pane {

namespace {

// The one that listens: Pane's UI has one.
AppEvents* g_events = nullptr;

}  // namespace

bool MayTakeFront() {
  return !model::TestHook("PANE_AUTOMATION_DIR") ||
         model::TestHook("PANE_AUTOMATION_FOREGROUND") ==
             std::optional<std::string>("1");
}

AppEvents::AppEvents(base::RepeatingClosure reopen, OpenURLs open_urls)
    : reopen_(std::move(reopen)), open_urls_(std::move(open_urls)) {
  g_events = this;
  HearOtherLaunches();
}

AppEvents::~AppEvents() {
  if (g_events == this) {
    g_events = nullptr;
  }
}

// static
void AppEvents::ChromeStarted() {
  if (g_events) {
    g_events->HearOtherLaunches();
  }
}

void AppEvents::Reopen() {
  reopen_.Run();
}

// Web pages and files: nothing else reaches the engine.
void AppEvents::OpenURLsHanded(std::vector<GURL> urls) {
  PANE_TRACE("callback", "PaneBrowserApplicationDelegate.openURLs",
             base::DictValue().Set("count", static_cast<int>(urls.size())));
  std::erase_if(urls, [](const GURL& url) { return !IsHandedPage(url); });
  if (!urls.empty()) {
    open_urls_.Run(std::move(urls));
  }
}

void AppEvents::HearOtherLaunches() {
  if (!ChromeProcessSingleton::IsSingletonInstance()) {
    return;
  }
  // The singleton's queue of what came while Pane started runs through it
  // as it unlocks.
  ChromeProcessSingleton::GetInstance()->Unlock(
      base::BindRepeating(&AppEvents::OtherLaunch, weak_factory_.GetWeakPtr()));
}

// Pane is going: the other launch starts on its own, once this one has let
// the data go, as Chrome's own refuses one while it shuts down.
// static
bool AppEvents::OtherLaunch(base::WeakPtr<AppEvents> events,
                            base::CommandLine command_line,
                            const base::FilePath& directory) {
  if (!events) {
    return false;
  }
  // Out of the singleton's message, as Chrome's own goes on (on Windows it
  // is a SendMessage's handler, where no call may leave the process), and
  // off the UI thread, since a relative file's path is looked for on the
  // disk.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&HandedPages, std::move(command_line), directory),
      base::BindOnce(&AppEvents::OpenLaunch, std::move(events)));
  return true;
}

void AppEvents::OpenLaunch(std::vector<GURL> pages) {
  if (pages.empty()) {
    Reopen();
    return;
  }
  OpenURLsHanded(std::move(pages));
}

}  // namespace pane
