// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/pane_main_delegate.h"

#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/process/process.h"
#include "chrome/browser/profiles/profile_shortcut_manager.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "content/public/common/content_switches.h"
#include "pane/app/data_launch.h"
#include "pane/app/install_switches.h"
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
  // What a process logs before Chrome sets its logging up (InitChromeLogging,
  // which on Windows waits for the sandbox), Pane's launch included, goes to
  // the system's log and stderr, the Mac's default: Windows' own is a
  // debug.log beside chrome.exe, in the app's folder, which an install keeps
  // to the package's files (ADR 0127).
  logging::LoggingSettings early_logging;
  early_logging.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  logging::InitLogging(early_logging);

  base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kProcessType)) {
    // Chrome's profile shortcuts stay off. On Windows they run where the user
    // data folder is Chromium's default, as Pane's install makes it, and once
    // Pane's first profile appeared they turned the desktop's Pane.lnk the
    // install made into a shortcut of Chrome's own for the Default profile,
    // "Your Chromium - Chromium", badged, and wrote a Google Profile.ico into
    // each profile's folder (ProfileShortcutManagerWin, ADR 0010): Pane's
    // shortcuts are its own (ADR 0127). Chromium keeps one switch for it, the
    // one its unit tests take, which only flips the check the profile manager
    // asks as it is made: so here, before Chrome's browser process exists.
    // Nothing on the Mac, where Chrome makes none.
    ProfileShortcutManager::DisableForUnitTests();
    // Pane's install, its registration, and its uninstall, which open no
    // data and start no engine (ADR 0127).
    if (std::optional<int> exit_code = RunInstallSwitch(command_line)) {
      // ContentMain ends the process for a code of 0 or more alone
      // (content_main.cc): a failure's, an HRESULT the installer shows, is
      // negative, and went on to start the engine, which ended on a check
      // and hid the code (commit 4fb6caa). The process ends here with it.
      if (*exit_code < 0) {
        base::Process::TerminateCurrentProcessImmediately(*exit_code);
      }
      return exit_code;
    }
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
