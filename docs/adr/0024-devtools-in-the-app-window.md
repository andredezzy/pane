# DevTools dock beside the tab in the app's window

DevTools dock beside their tab in the app's window, and undocked they open in a window of Pane's, never in Chrome's DevTools window, like every other Chrome surface of a tab (ADR 0021).

**Docked.** Each hosted `Browser` has a `DevtoolsUIController` of Pane's (`DockingDevtoolsController`, `pane/browser/ui/browser_window.cc`), whose `UpdateDevtools` patch 0004 makes virtual in Chromium, the one edit this takes, so `DevToolsWindow` asks it to dock as it asks Chrome's. It tells the window (`WindowHost::Delegate::DevToolsChanged`), and the root view shows DevTools in a second `WebView` filling the page's place, the page over them where their splitter puts it, or nowhere while they cover it, as Chrome lays them out in its contents container (`ApplyDevToolsContentsResizingStrategy`, `pane/ui/page/devtools.h`); the card's corners go on whichever meets them (ADR 0073). Inspecting a DevTools window docks nothing, as in Chrome.

**Undocked,** Chrome's DevTools `Browser` gets a `HostedBrowserWindow` in a Views window of Pane's that shows only the DevTools page (`pane/ui/page/devtools_window`, its widget named "PaneDevTools"): 960 by 640, titled DevTools, in the middle of the screen, in front only where Pane may take the front (ADR 0012). Chrome builds that Browser before it can join a window, so it joins a task later. Docking DevTools again closes the window.

**The keys.** ⇧⌘I closes docked DevTools, as their own close button does, and opens the others as they last were, docked or in their window, bringing that window forward if it is open, where the Swift's key did nothing (`ToggleDevTools`), timed to their first paint ("Open developer tools"). ⌥⌘I, Chrome's and Safari's key for the page's tools, does the same: a hidden item of View's that keeps its key, as ⌘= is Zoom In's, while the menu shows ⇧⌘I (the user's call, 2026-11-10).

A tab with DevTools open does not sleep (ADR 0041). The undocked window is a window of its own, which the PIN screen does not cover, so behind the Lock the menu cannot open DevTools (ADR 0042).

The controller replaced Ardia's `chromium-devtools-*` seam and, in the review of 0.18.0, Pane's own hooks in `devtools_window.cc` and `devtools_window_views.cc`, which returned to the original (ADR 0004). Until 0.20.0 the engine kept the undocked window itself, an engine-owned window showing the DevTools page (`devtools_window_host.mm`).

The `devtools` scenario docks DevTools with ⇧⌘I, watches for any window of Chrome's while they are open, and closes them with ⇧⌘I again. `devtools-undocked` undocks them into Pane's window and docks them again, which closes it.
