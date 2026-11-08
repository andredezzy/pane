# URLs other apps hand over open in Pane's window, and their sign-ins stay in macOS's sheet

What another app hands Pane opens as new tabs in its window, in the order given, in the profile in use, the last one selected: a link clicked in Mail or Slack with Pane as the default browser, a file opened with Pane, `open -a Pane <url>`. The bundle registers http, https, and file, and the HTML and image types, as Chrome's does. Until then only a URL on the command line opened (`Pane https://…`). Launch Services delivers the others to Chrome's AppController (`-application:openURLs:`), which opened them in a Browser of Chrome's, behind Pane's window and never seen. The research comparing Pane with Aside and Dia found it: both open such links in their own window.

Patch 0002 has a hosted engine's AppController post `PANE_OPEN_URLS_NOTIFICATION` (`pane/common/host.h`) instead, as it already does for reopen. `PaneBrowserApplication` observes it from its start, holds what arrives before the engine started (the app launched to open them), and calls the delegate's new `-browserApplication:openURLs:`. The app opens the web pages and files among them through the path the command-line URL takes, so they wait behind the PIN lock as it does, and shows the window again if it was closed.

Chrome opens them in the last profile used; Pane's profile in use is the one whose tab is selected, and on a first launch with none, the "Personal" profile it makes.

**Another app's sign-in is not handed over.** Pane's bundle does not say it supports `ASWebAuthenticationSession` (`ASWebAuthenticationSessionWebBrowserSupportCapabilities`, which Chromium's Info.plist sets and `scripts/package.sh` removes). An app that signs its user in through one (Slack, Zoom, Notion, and others) gets macOS's own sign-in sheet, whatever the default browser, as with Dia, which leaves the key out too.

With the key, macOS handed such a sign-in to the default browser, and Chrome's `AuthSessionRequest` (`chrome/browser/mac/auth_session_request.mm`) opened it in a popup Browser of its own. Pane's window hosts every popup Browser by moving its tab into the profile's Browser and closing the popup (ADR 0014, `ForwardToPaneWindow`), but the request keeps the popup's Browser to close the page when the sign-in ends: by then it is gone, so a completed or cancelled sign-in reached a freed Browser. The same research found it, reading the code; no scenario had covered it.

What was rejected:

- **A function the app sets in the engine, called by the AppController:** chrome/ may not depend on //pane (`host.h` is header-only for that), and a notification is how the AppController already reaches the hosted app.
- **The first profile, as the command-line URL used:** Chrome opens them where the user is.
- **Keeping a sign-in's popup a window of Chrome's:** a window of Chrome's look among Pane's, and the only one.
- **A sign-in in a tab of Pane's window, with the request patched to find the tab's Browser:** Chrome's security team requires the sign-in's address to show and not be editable ("must not be changed without consultation with the security team"), and a tab's address bar is editable.

How it is checked:

- **The open-urls scenario** hands a running Pane a page and a file through Launch Services (`tests/tools/open-urls.swift`, without activating it), which sends them as an Apple Event each: both open in the profile in use, the last one selected, and no other window shows.
- **A cold launch** (`open -n -g -a app/build/Pane.app <url>` with a throwaway home and data directory) delivered the URL while the engine started; it opened after `didStart`, in the "Personal" profile made for it.
- **The security scenario** hands one over behind the PIN lock: nothing opens until the PIN does, then it opens after the launch URL.
- **`plutil -p Pane.app/Contents/Info.plist`** of a packaged app lists no `ASWebAuthenticationSessionWebBrowserSupportCapabilities`.
