# DevTools dock beside the tab in the app's window

DevTools dock beside their tab in the app's window, and undocked they open in a window of Pane's, never in Chrome's DevTools window, like every other Chrome surface of a tab (ADR 0021). Pane's own docking hooks in `DevToolsWindow` (`engine/patches/0004-pane-ui-hooks.patch`) replace Ardia's `chromium-devtools-*` seam and cover undocked DevTools too. Undocked, Chrome's DevTools `Browser` gets a `HostedBrowserWindow` in an engine-owned window (`engine/pane/browser/ui/devtools_window_host.mm`) that shows only the DevTools page. Chrome builds that Browser before it can join a window, so it joins a task later.

A tab with DevTools open does not sleep (ADR 0041). The undocked window is the engine's, which the PIN screen does not cover, so behind the PIN lock the menu cannot open DevTools (ADR 0042).

The `devtools` scenario docks DevTools with ⇧⌘I, watches for any window of Chrome's while they are open, and closes them with ⇧⌘I again. `devtools-undocked` undocks them into Pane's window and docks them again, which closes it.
