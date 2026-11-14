// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_APP_EVENTS_H_
#define PANE_UI_APP_APP_EVENTS_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace pane {

// Whether Pane may take the front from the app in use: always, but for a
// scenario's copy, which takes it only when the runner allows it (run.ts
// --foreground, PANE_AUTOMATION_FOREGROUND), so whoever works on the Mac keeps
// their keys (ADR 0012).
bool MayTakeFront();

// Whether the window's close quits Pane: on Windows, as an app quits there
// once its last window closes, Chrome among them; on the Mac Pane stays, in
// the Dock, as AppKit's apps do (app_events_mac.mm, app_events_win.cc). Quit's
// own keys ask first (ADR 0053); the window's close asks its pages, as ever.
bool ClosingTheWindowQuits();

// What the system asks of Pane from outside its window, as Chrome's app
// controller hands it over (pane/common/host.h): the Dock's reopen, which
// shows the window again, a new one if it was closed; and the web pages and
// files another app hands Pane (a link clicked in Mail, `open -a Pane
// <url>`), which open as tabs in it (ADR 0109). Heard while it lives, which
// is also while a scenario's copy, made active by its own launch, gives the
// front back at once unless it may take it.
//
// On every system, another launch of Pane on the same data, which Chromium's
// process singleton hands over and quits (on Windows how another app hands
// Pane a link: the shell starts Pane with it; from a terminal on the Mac),
// opens the pages it names in the same way, or, naming none, shows the window
// as the Dock's reopen does. Chrome's own startup would take it otherwise,
// which opens nothing for Pane (--no-startup-window: Pane's windows are
// Pane's).
class AppEvents {
 public:
  using OpenURLs = base::RepeatingCallback<void(std::vector<GURL>)>;

  static std::unique_ptr<AppEvents> Listen(base::RepeatingClosure reopen,
                                           OpenURLs open_urls);

  // Chrome's start has ended (ChromeBrowserMainParts::PostBrowserStart), and
  // gave the process singleton Chrome's own callback, which replaces the one
  // Pane gave it as it listened, before: Pane's goes back in, before the main
  // loop runs. Between the two nothing runs, and Pane's first took what came
  // while Pane started.
  static void ChromeStarted();

  AppEvents(const AppEvents&) = delete;
  AppEvents& operator=(const AppEvents&) = delete;
  virtual ~AppEvents();

 protected:
  AppEvents(base::RepeatingClosure reopen, OpenURLs open_urls);

  void Reopen();
  void OpenURLsHanded(std::vector<GURL> urls);

 private:
  // Pane's callback for the process singleton.
  void HearOtherLaunches();
  static bool OtherLaunch(base::WeakPtr<AppEvents> events,
                          base::CommandLine command_line,
                          const base::FilePath& directory);
  void OpenLaunch(std::vector<GURL> pages);

  const base::RepeatingClosure reopen_;
  const OpenURLs open_urls_;
  base::WeakPtrFactory<AppEvents> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_APP_APP_EVENTS_H_
