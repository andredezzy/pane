// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#import <Foundation/Foundation.h>

#include <string>

#include "base/apple/foundation_util.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/chrome_devtools_manager_delegate.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/network_service_instance.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/window_host.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"

#import "pane/browser/browser_application_internal.h"

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
  NSString* path = [NSBundle.mainBundle pathForResource:@"crl-set" ofType:nil];
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
          base::apple::NSStringToFilePath(path)),
      base::BindOnce([](const std::string& bytes) {
        if (!bytes.empty()) {
          content::GetCertVerifierServiceFactory()->UpdateCRLSet(
              base::as_byte_span(bytes), base::BindOnce([] {
                PANE_TRACE("engine", "PaneEngine.crlSetApplied");
              }));
        }
      }));
}

}  // namespace

BrowserMainExtraParts::BrowserMainExtraParts() = default;

BrowserMainExtraParts::~BrowserMainExtraParts() = default;

void BrowserMainExtraParts::PreProfileInit() {
  LoadBundledCRLSet();
}

void BrowserMainExtraParts::PostProfileInit(Profile* profile,
                                            bool is_initial_profile) {
  if (!profile_manager_observation_.IsObserving()) {
    profile_manager_observation_.Observe(
        g_browser_process->profile_manager());
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
  // ⌘Q quits at once, as it did in the Electron app. Chrome asks to hold it
  // (the "Warn Before Quitting" pref, on by default), in a panel of its own.
  g_browser_process->local_state()->SetBoolean(prefs::kConfirmToQuitEnabled,
                                               false);
  // Contexts let go of their profiles as quitting starts, while the message
  // loop still runs, as Chrome's windows do when they close: profiles then
  // unload with their processes. Released after the loop, a profile could be
  // left for teardown with a live service worker process.
  //
  // Quitting also outranks a remote debugging client: Chrome started without
  // a window stays up for one until it closes the browser itself, but Pane's
  // windows are the app's, and Quit is the user's call.
  app_terminating_subscription_ =
      browser_shutdown::AddAppTerminatingCallback(base::BindOnce([] {
    ChromeDevToolsManagerDelegate::AllowBrowserToClose();
    [PaneBrowserApplication.sharedApplication engineWillShutDown];
  }));
  [PaneBrowserApplication.sharedApplication engineDidStart];
}

void BrowserMainExtraParts::PostMainMessageLoopRun() {
  [PaneBrowserApplication.sharedApplication engineWillShutDown];
}

}  // namespace pane
