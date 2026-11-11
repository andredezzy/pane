// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/pane_main_delegate.h"

#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "content/public/common/content_switches.h"
#include "pane/app/data_launch.h"
#include "pane/app/views_launch.h"
#include "pane/browser/content_browser_client.h"

namespace pane {

PaneMainDelegate::PaneMainDelegate(const StartupTimestamps& timestamps)
    : ChromeMainDelegate(timestamps) {}

PaneMainDelegate::~PaneMainDelegate() = default;

// The browser process starts Pane's UI (StartViewsLaunch), set up before
// Chrome reads its command line. Every launch does, a relaunch too: one that
// copied the engine's own command line, --pane-hosted in it, would otherwise
// start the engine with no data and no window.
std::optional<int> PaneMainDelegate::BasicStartupComplete() {
  base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kProcessType)) {
    if (std::optional<int> exit_code = StartViewsLaunch(command_line)) {
      return exit_code;
    }
  }
  return ChromeMainDelegate::BasicStartupComplete();
}

// The engine has stopped and let go of its singleton: a Pane started now
// waits for the data's lock, which this one lets go of once the volume
// detaches.
void PaneMainDelegate::ProcessExiting(const std::string& process_type) {
  ChromeMainDelegate::ProcessExiting(process_type);
  if (process_type.empty() && IsViewsLaunch()) {
    DataDidStop();
  }
}

// Mirrors ChromeMainDelegate::CreateContentBrowserClient with Pane's client;
// keep the two in step on Chromium upgrades.
content::ContentBrowserClient* PaneMainDelegate::CreateContentBrowserClient() {
  chrome_content_browser_client_ = std::make_unique<ContentBrowserClient>();
  CHECK(sampling_profiler_);
  chrome_content_browser_client_->SetSamplingProfiler(
      std::move(sampling_profiler_));
  return chrome_content_browser_client_.get();
}

std::unique_ptr<ChromeMainDelegate> CreateMainDelegate(
    const StartupTimestamps& timestamps) {
  return std::make_unique<PaneMainDelegate>(timestamps);
}

}  // namespace pane
