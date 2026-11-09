// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_APP_EVENTS_H_
#define PANE_UI_APP_APP_EVENTS_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "url/gurl.h"

namespace pane {

// Whether Pane may take the front from the app in use: always, but for a
// scenario's copy, which takes it only when the runner allows it (run.ts
// --foreground, PANE_AUTOMATION_FOREGROUND), so whoever works on the Mac keeps
// their keys (ADR 0012).
bool MayTakeFront();

// What the system asks of Pane from outside its window, as Chrome's app
// controller hands it over (pane/common/host.h): the Dock's reopen, which
// shows the window again, a new one if it was closed; and the web pages and
// files another app hands Pane (a link clicked in Mail, `open -a Pane
// <url>`), which open as tabs in it (ADR 0109). Heard while it lives, which
// is also while a scenario's copy, made active by its own launch, gives the
// front back at once unless it may take it.
class AppEvents {
 public:
  using OpenURLs = base::RepeatingCallback<void(std::vector<GURL>)>;

  static std::unique_ptr<AppEvents> Listen(base::RepeatingClosure reopen,
                                           OpenURLs open_urls);

  AppEvents(const AppEvents&) = delete;
  AppEvents& operator=(const AppEvents&) = delete;
  virtual ~AppEvents() = default;

 protected:
  AppEvents() = default;
};

}  // namespace pane

#endif  // PANE_UI_APP_APP_EVENTS_H_
