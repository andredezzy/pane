// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_main_extra_parts.h"

#include <memory>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/devtools/chrome_devtools_manager_delegate.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/grit/branded_strings.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "pane/app/windows_integration.h"
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
