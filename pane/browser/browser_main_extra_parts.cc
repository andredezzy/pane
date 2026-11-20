// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/chrome_devtools_manager_delegate.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/network_service_instance.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "pane/app/data_launch.h"
#include "pane/app/engine_switches.h"
#include "pane/browser/bundled_crl_set.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profiles.h"
#include "pane/browser/system_proxy.h"
#include "pane/browser/ui/install_hooks.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/app/ui_app.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"

namespace pane {

namespace {

// Chrome's CRLSet, which the bundle carries (package.sh puts the current one
// in it as the app is built): the certificates revoked, and the keys blocked,
// since the root store was made. Chrome's component updater fetches it from
// Google every few hours; Pane makes no request of its own (ADR 0040), so it
// updates with the app (ADR 0111). Read off the main thread, and handed to
// the certificate verifier before any profile loads. The verifier parses it
// once startup is over (a best-effort task), as it does Chrome's component.
void LoadBundledCRLSet() {
  const std::optional<base::FilePath> path = BundledCRLSet();
  if (!path) {
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING, base::MayBlock()},
      base::BindOnce(
          [](const base::FilePath& file) {
            std::string bytes;
            base::ReadFileToString(file, &bytes);
            return bytes;
          },
          *path),
      base::BindOnce([](const std::string& bytes) {
        if (!bytes.empty()) {
          content::GetCertVerifierServiceFactory()->UpdateCRLSet(
              base::as_byte_span(bytes), base::BindOnce([] {
                PANE_TRACE("engine", "PaneEngine.crlSetApplied");
              }));
        }
      }));
}

// Spellcheck's Hunspell dictionaries, which Chrome downloads for a language
// the system's spellchecker lacks (Windows'), go in the user data dir, Pane's
// data, which the panic wipe takes, where Chromium puts one on Windows when it
// can't write beside chrome.exe (SaveDictionaryData) and keeps them on every
// other system. Beside chrome.exe (chrome_paths.cc, DIR_APP_DICTIONARIES) is
// the app's own folder: one an update replaces, which would download each
// again, and one the wipe leaves, with the languages' names. Spellcheck alone
// reads the path, and makes nothing in it until it saves a dictionary.
void KeepDictionariesInUserData() {
  base::FilePath user_data;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    base::PathService::OverrideAndCreateIfNeeded(chrome::DIR_APP_DICTIONARIES,
                                                 user_data,
                                                 /*is_absolute=*/true,
                                                 /*create=*/false);
  }
}

// Chromium makes its crash reports' folder as anything asks where it is
// (chrome_paths.cc's DIR_CRASH_DUMPS, create_dir), and a profile's deletion
// asks, as it clears the crash list with the profile's history
// (ChromeBrowsingDataRemoverDelegate, CreateCrashUploadList()->Clear()): an
// empty Crashpad folder appeared in Windows' engine folder at Delete profile
// (found by hand on Windows, 2026-11-16; commit f81ca5f), and in Application
// Support's Chromium folder on the Mac, outside Pane's data. Pane keeps no
// crash reports (ADR 0115), so the folder is named in the user data dir, Pane's
// data, and never made: the clear finds no log there and no database, and does
// nothing. Before any profile is deleted, those a quit left marked included.
void KeepNoCrashFolder() {
  base::FilePath user_data;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    base::PathService::OverrideAndCreateIfNeeded(
        chrome::DIR_CRASH_DUMPS,
        user_data.Append(FILE_PATH_LITERAL("Crashpad")),
        /*is_absolute=*/true, /*create=*/false);
  }
}

// Chrome's background mode stays off: on Windows, a profile with an extension
// that asks the "background" permission put Chrome's own icon in the
// notification area, whose menu opens Chrome's task manager, About, and a
// window per profile, all Chrome's UI outside Pane's window (ADR 0010), and
// kept the engine running for Chrome's background apps. Pane's process lives
// until Pane quits, background apps or not (StartPlatform). Written before
// the background mode's manager reads it, at every launch; the Mac makes no
// icon of it, and takes the same pref.
void KeepBackgroundModeOff(PrefService& local_state) {
  local_state.SetBoolean(prefs::kBackgroundModeEnabled, false);
}

}  // namespace

BrowserMainExtraParts::BrowserMainExtraParts() = default;

BrowserMainExtraParts::~BrowserMainExtraParts() = default;

// The earliest of the browser's start, in the process that holds Pane's data
// alone: a second launch hands its command line over and exits before it.
void BrowserMainExtraParts::PreEarlyInitialization() {
  StartClearingLeftoverNotifications();
}

// Before the browser's network contexts exist (ADR 0040), before the
// background mode's manager does, and before any profile's deletion.
void BrowserMainExtraParts::PreCreateThreads() {
  KeepNoCrashFolder();
  UsePanesWords();
  SystemProxy::HoldLocalState(*g_browser_process->local_state());
  KeepBackgroundModeOff(*g_browser_process->local_state());
}

// Chromium's own reading of the system's proxy settings, as Chrome makes it
// for its network contexts, on the UI thread.
void BrowserMainExtraParts::PostCreateThreads() {
  system_proxy_ = std::make_unique<SystemProxy>(
      *g_browser_process->local_state(),
      net::ProxyConfigService::CreateSystemProxyConfigService(
          base::SingleThreadTaskRunner::GetCurrentDefault()));
}

void BrowserMainExtraParts::PreProfileInit() {
  // A toast's activation this launch carries, out of the command line before
  // Chrome's startup reads it to open the toast's profile, which waits for
  // Pane's lock and keys: Pane's UI hands it to the site (ADR 0109).
  launch_toast_ =
      TakeNotificationSwitches(*base::CommandLine::ForCurrentProcess());
  // Before the initial profile, whose display service makes Chrome's bridge:
  // in this process alone, the one that holds Pane's data (a second launch
  // hands its command line over and exits before the browser starts).
  ClearLeftoverNotifications();
  KeepDictionariesInUserData();
  LoadBundledCRLSet();
  BuildProfileServices();
}

void BrowserMainExtraParts::PostProfileInit(Profile* profile,
                                            bool is_initial_profile) {
  if (!profile_manager_observation_.IsObserving()) {
    profile_manager_observation_.Observe(g_browser_process->profile_manager());
  }
  OnProfileAdded(profile);
}

// The spellchecker hands its dictionaries only to renderers that start after
// it exists. Chrome brings it up with sync; Pane does as the profile arrives,
// before any of its renderers.
void BrowserMainExtraParts::OnProfileAdded(Profile* profile) {
  SpellcheckServiceFactory::GetForContext(profile);
}

void BrowserMainExtraParts::OnProfileManagerDestroying() {
  profile_manager_observation_.Reset();
}

void BrowserMainExtraParts::PostBrowserStart() {
  InstallUIHooks();
  // The data opens with the thread pool, before Pane's UI.
  OpenData();
  UiApp::Start(std::move(launch_toast_));
  StartPlatform();
  // Quitting outranks a remote debugging client: Chrome started without a
  // window stays up for one until it closes the browser itself, but Pane's
  // windows are Pane's, and Quit is the user's call (ADR 0010). Pane's UI
  // lets its profiles go as quitting starts (UiApp::Start).
  app_terminating_subscription_ = browser_shutdown::AddAppTerminatingCallback(
      base::BindOnce(&ChromeDevToolsManagerDelegate::AllowBrowserToClose));
}

// Chrome's start is over, Chrome's own handlers given, and the main loop runs
// next: another launch of Pane comes to Pane's window from now on.
void BrowserMainExtraParts::PreMainMessageLoopRun() {
  AppEvents::ChromeStarted();
}

void BrowserMainExtraParts::PostMainMessageLoopRun() {
  DataWillStop();
  system_proxy_.reset();
}

}  // namespace pane
