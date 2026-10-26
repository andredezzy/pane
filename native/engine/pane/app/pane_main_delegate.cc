// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/pane_main_delegate.h"

#include <utility>

#include "base/check.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "pane/browser/content_browser_client.h"

namespace pane {

PaneMainDelegate::PaneMainDelegate(const StartupTimestamps& timestamps)
    : ChromeMainDelegate(timestamps) {}

PaneMainDelegate::~PaneMainDelegate() = default;

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
