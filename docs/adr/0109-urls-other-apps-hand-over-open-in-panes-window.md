# URLs other apps hand over open in Pane's window

What another app hands Pane opens as new tabs in its window, in the order given, in the profile in use, the last one selected: a link clicked in Mail or Slack with Pane as the default browser, a file opened with Pane, `open -a Pane <url>`. The bundle registers http, https, and file, and the HTML and image types, as Chrome's does. Until now only a URL on the command line opened (`Pane https://…`). Launch Services delivers the others to Chrome's AppController (`-application:openURLs:`), which opened them in a Browser of Chrome's, behind Pane's window and never seen. The research comparing Pane with Aside and Dia found it: both open such links in their own window.

Patch 0002 has a hosted engine's AppController post `PANE_OPEN_URLS_NOTIFICATION` (`pane/common/host.h`) instead, as it already does for reopen. `PaneBrowserApplication` observes it from its start, holds what arrives before the engine started (the app launched to open them), and calls the delegate's new `-browserApplication:openURLs:`. The app opens the web pages and files among them through the path the command-line URL takes, so they wait behind the PIN lock as it does, and shows the window again if it was closed.

Chrome opens them in the last profile used; Pane's profile in use is the one whose tab is selected, and on a first launch with none, the "Personal" profile it makes.

What was rejected:

- **A function the app sets in the engine, called by the AppController:** chrome/ may not depend on //pane (`host.h` is header-only for that), and a notification is how the AppController already reaches the hosted app.
- **The first profile, as the command-line URL used:** Chrome opens them where the user is.

How it is checked:

- **The open-urls scenario** hands a running Pane a page and a file through Launch Services (`tests/tools/open-urls.swift`, without activating it), which sends them as an Apple Event each: both open in the profile in use, the last one selected, and no other window shows.
- **A cold launch** (`open -n -g -a app/build/Pane.app <url>` with a throwaway home and data directory) delivered the URL while the engine started; it opened after `didStart`, in the "Personal" profile made for it.
- **The security scenario** hands one over behind the PIN lock: nothing opens until the PIN does, then it opens after the launch URL.
