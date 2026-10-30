# Other apps' sign-ins stay in macOS's sheet

Pane's bundle no longer says it supports `ASWebAuthenticationSession` (`ASWebAuthenticationSessionWebBrowserSupportCapabilities`, which Chromium's Info.plist sets and `engine/scripts/package.sh` now removes). An app that signs its user in through one (Slack, Zoom, Notion, and others) gets macOS's own sign-in sheet, whatever the default browser, as with Dia, which leaves the key out too.

With the key, macOS handed such a sign-in to the default browser, and Chrome's `AuthSessionRequest` (`chrome/browser/mac/auth_session_request.mm`) opened it in a popup Browser of its own. Pane's window hosts every popup Browser by moving its tab into the profile's Browser and closing the popup (ADR 0014, `ForwardToPaneWindow`), but the request keeps the popup's Browser to close the page when the sign-in ends: by then it is gone, so a completed or cancelled sign-in reached a freed Browser. The research comparing Pane with Aside and Dia found it, reading the code; no scenario had covered it.

What was rejected:

- **Keeping the popup a window of Chrome's:** a window of Chrome's look among Pane's, and the only one.
- **A tab of Pane's window, with the request patched to find the tab's Browser:** Chrome's security team requires the sign-in's address to show and not be editable ("must not be changed without consultation with the security team"), and a tab's address bar is editable.

How it is checked:

- **`plutil -p Pane.app/Contents/Info.plist`** of a packaged app lists no `ASWebAuthenticationSessionWebBrowserSupportCapabilities`.
