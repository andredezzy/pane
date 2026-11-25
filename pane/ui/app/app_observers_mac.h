// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_APP_OBSERVERS_MAC_H_
#define PANE_UI_APP_APP_OBSERVERS_MAC_H_

// AppKit's notifications that the Mac's AppEvents hears (app_events_mac.cc),
// each heard until its runner is gone.

#include <vector>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "url/gurl.h"

namespace pane {

// What Chrome's app controller hands Pane, posted as patch 0002's two
// notifications (pane/common/host.h): the Dock's reopen, to `reopen`, and the
// web pages and files another app hands it, to `open_urls`.
base::ScopedClosureRunner HearHandOvers(
    base::RepeatingClosure reopen,
    base::RepeatingCallback<void(std::vector<GURL>)> open_urls);

// The front given back at once, each time Pane becomes active, to the app it
// took it from: NSApp's deactivate leaves the app active since macOS 14, so
// the other app is activated, as the active app may do.
base::ScopedClosureRunner GiveTheFrontBack();

}  // namespace pane

#endif  // PANE_UI_APP_APP_OBSERVERS_MAC_H_
