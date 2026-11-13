// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_COMMON_HOST_H_
#define PANE_COMMON_HOST_H_

// The contract between Pane's launch and the Chrome code patched to honor it
// (chromium/patches, ADR 0010). Header-only constants, so patched Chrome files
// can use them without depending on //pane.

// Set by Pane's launch (pane/app/engine_switches.h) on the browser process it
// runs Pane's UI in. Chrome's own UI (startup windows, the main menu, reopened
// windows, Handoff) stays off under it; a Chromium binary run without it, such
// as Chromium's own tests, keeps Chrome's.
#define PANE_HOSTED_SWITCH "pane-hosted"

// Posted by Chrome's AppController, instead of opening a window, when the user
// re-activates Pane (for example from the Dock).
#define PANE_SHOULD_REOPEN_NOTIFICATION "PaneShouldReopenNotification"

// Posted by Chrome's AppController, instead of opening them in a Browser of
// its own, with the URLs another app hands Pane (a link clicked in Mail, a
// file opened with Pane, `open -a Pane <url>`), as an NSArray of NSURL under
// PANE_OPEN_URLS_KEY.
#define PANE_OPEN_URLS_NOTIFICATION "PaneOpenURLsNotification"
#define PANE_OPEN_URLS_KEY "urls"

// The renderer's half of a profile's fingerprint, a JSON object, which Pane's
// browser hands each of the profile's renderers (pane/browser/fingerprints.h)
// and Blink reads (pane/renderer/pane_fingerprint.h).
#define PANE_FINGERPRINT_SWITCH "pane-fingerprint"

#endif  // PANE_COMMON_HOST_H_
