// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/pane_main_delegate.h"

#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "content/public/common/content_switches.h"
#include "pane/app/views_launch.h"
#include "pane/browser/content_browser_client.h"
#include "pane/common/host.h"

namespace pane {

PaneMainDelegate::PaneMainDelegate(const StartupTimestamps& timestamps)
    : ChromeMainDelegate(timestamps) {}

PaneMainDelegate::~PaneMainDelegate() = default;

// The Swift app starts the engine hosted (--pane-hosted); Chromium's loader
// starts it bare, and then Pane's own UI is the Views one (StartViewsLaunch),
// set up before Chrome reads its command line.
std::optional<int> PaneMainDelegate::BasicStartupComplete() {
  base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kProcessType) && !IsHosted()) {
    if (std::optional<int> exit_code = StartViewsLaunch(command_line)) {
      return exit_code;
    }
  }
  return ChromeMainDelegate::BasicStartupComplete();
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
