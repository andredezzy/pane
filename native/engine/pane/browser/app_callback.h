// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_APP_CALLBACK_H_
#define PANE_BROWSER_APP_CALLBACK_H_

namespace pane {

// Marks the engine calling the app from inside Chromium's own dispatch: a
// WebContents observer, a tab strip notification, a page's request. Chromium
// is mid-call on the tab there, so API calls that would destroy a tab or
// change a tab strip run on the next turn instead (PaneWebContents.h lists
// them). Scopes nest.
class ScopedAppCallback {
 public:
  ScopedAppCallback();
  ScopedAppCallback(const ScopedAppCallback&) = delete;
  ScopedAppCallback& operator=(const ScopedAppCallback&) = delete;
  ~ScopedAppCallback();
};

// Whether a ScopedAppCallback is live: the app is being called back.
bool InAppCallback();

}  // namespace pane

#endif  // PANE_BROWSER_APP_CALLBACK_H_
