// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include <memory>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "base/timer/elapsed_timer.h"
#include "base/values.h"
#include "chrome/browser/devtools/chrome_devtools_manager_delegate.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/grit/branded_strings.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "pane/app/windows_integration.h"
#include "pane/browser/engine_trace.h"
#include "ui/base/resource/resource_bundle.h"

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

// Chromium's words where they reach Windows, through the seams Chromium
// keeps: the installer's strings (installer::SetTranslationDelegate, which
// Chrome sets for its own in PreCreateMainMessageLoop, before this), so a
// toast finds Pane's Start entry by its name ("Pane.lnk", where Chromium's
// looks for "Chromium.lnk") and the shortcuts and Default apps say Pane; and
// a toast's settings item (ResourceBundle's override, which Chrome's
// variations use for theirs).
// static
void BrowserMainExtraParts::UsePanesWords() {
  windows::UsePanesWords();
  ui::ResourceBundle::GetSharedInstance().OverrideLocaleStringResource(
      IDS_WIN_NOTIFICATION_SETTINGS_CONTEXT_MENU_ITEM_NAME,
      u"Go to Pane notification settings");
}

// A crash closes nothing, so its toasts stayed in Windows' center, a site's
// words readable there without the PIN, until the next Exit (the user's
// call, 2026-11-18; ADR 0109). As this process starts, no toast of Pane's
// there is its own: each goes, before any profile can show one and before
// Chrome's bridge is made, at the initial profile's load. The bridge takes
// into its 10-minute check, as it starts, each toast it finds of an
// AppUserModelID it already knows, and reports each one gone as closed by
// its user, loading the profile through Chrome's profile manager, outside
// Pane's lock: this Chromium's starts knowing none, and one that read Pane's
// at its start finds none left. A crash's toast clicked starts Pane with the
// click, which Windows takes out of the center and Pane holds for the PIN.
// On the UI thread, where COM is Chrome's (ScopedOleInitializer), so it is
// done before the profile loads; the PIN screen's first frame waits for it.
// static
void BrowserMainExtraParts::ClearLeftoverNotifications() {
  const base::ElapsedTimer clearing;
  const bool cleared = windows::ClearToasts();
  PANE_TRACE("engine", "PaneEngine.leftoverNotificationsCleared",
             base::DictValue()
                 .Set("cleared", cleared)
                 .Set("ms", clearing.Elapsed().InMillisecondsF()));
}

// Chrome on Windows keeps its browser process while a Browser of its own or a
// keep-alive holds it, and Pane may have no profile open, so no Browser: a
// keep-alive of Pane's own holds it, as Chrome's AppController holds one on
// the Mac, under that origin, until a quit starts closing every Browser
// (chrome::AttemptUserExit), and again if a page's beforeunload cancels it.
// Closing Pane's window quits it, as Windows' apps quit once their last
// window closes (ClosingTheWindowQuits, UiApp::WindowClosed; ADR 0053).
// Chrome warns before no quit on Windows: "Warn Before Quitting" is the Mac's
// pref alone.
//
// A scenario's launch, with remote debugging and no startup window, holds
// DevTools' keep-alive too, which Chrome lets go only at DevTools'
// Browser.close (ChromeDevToolsManagerDelegate): Quit lets it go as well, so
// Pane's Quit ends a scenario's process as it ends a user's, who never has
// remote debugging (ADR 0115). On the Mac, AppKit's quit ends the process
// whatever holds it.
//
// At Exit every toast of Pane's leaves Windows' center, as Chrome's Mac
// bridge takes every notification out of macOS's center at quit
// (~NotificationPlatformBridgeMac; ADR 0109): Windows' bridge leaves them,
// and a site's words stayed readable there with Pane closed (a crash's go as
// Pane next starts, ClearLeftoverNotifications).
// Once quitting can't be cancelled any more, the main loop still running,
// off the UI thread, and before the thread pool ends, which waits for it. A
// profile's own leave at each Lock through Chrome's own close (profiles.cc),
// whose bookkeeping a running Pane keeps; at Exit it goes with the process.
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
  static base::NoDestructor<base::CallbackListSubscription> exiting(
      browser_shutdown::AddAppTerminatingCallback(base::BindOnce([] {
        base::ThreadPool::PostTask(
            FROM_HERE,
            {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
             base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
            base::BindOnce(base::IgnoreResult(&windows::ClearToasts)));
      })));
}

}  // namespace pane
