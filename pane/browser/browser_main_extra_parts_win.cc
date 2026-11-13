// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include <memory>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/devtools/chrome_devtools_manager_delegate.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"

namespace pane {

namespace {

// Holds the browser process, or lets it go.
void KeepProcess(bool keep) {
  static base::NoDestructor<std::unique_ptr<ScopedKeepAlive>> keep_alive;
  *keep_alive =
      keep ? std::make_unique<ScopedKeepAlive>(KeepAliveOrigin::APP_CONTROLLER,
                                               KeepAliveRestartOption::DISABLED)
           : nullptr;
}

}  // namespace

// Chrome on Windows keeps its browser process while a Browser of its own or a
// keep-alive holds it, and Pane may have no profile open, so no Browser: a
// keep-alive of Pane's own holds it, as Chrome's AppController holds one on
// the Mac, under that origin, until a quit starts closing every Browser
// (chrome::AttemptUserExit), and again if a page's beforeunload cancels it.
// Whether closing the last window quits, as Windows' apps do, is W4's
// (docs/plans/views-migration.md, ADR 0053). Chrome warns before no quit on
// Windows: "Warn Before Quitting" is the Mac's pref alone.
//
// A scenario's launch, with remote debugging and no startup window, holds
// DevTools' keep-alive too, which Chrome lets go only at DevTools'
// Browser.close (ChromeDevToolsManagerDelegate): Quit lets it go as well, so
// Pane's Quit ends a scenario's process as it ends a user's, who never has
// remote debugging (ADR 0115). On the Mac, AppKit's quit ends the process
// whatever holds it.
// static
void BrowserMainExtraParts::StartPlatform() {
  KeepProcess(true);
  static base::NoDestructor<base::CallbackListSubscription> quitting(
      chrome::AddClosingAllBrowsersCallback(
          base::BindRepeating([](bool closing) {
            KeepProcess(!closing);
            if (closing) {
              ChromeDevToolsManagerDelegate::AllowBrowserToClose();
            }
          })));
}

}  // namespace pane
