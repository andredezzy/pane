// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_DEVTOOLS_WINDOW_H_
#define PANE_UI_PAGE_DEVTOOLS_WINDOW_H_

namespace pane {

class WindowHost;

// The name of undocked DevTools' window, which the scenarios tell apart from
// Pane's own and from Chrome's (role "devtools").
inline constexpr char kDevToolsWindowName[] = "PaneDevTools";

// Undocked DevTools (ADR 0024): a window of Pane's, a Views window as its
// own, showing the DevTools page alone, never Chrome's DevTools window: 960
// by 640, titled DevTools, in the middle of the screen, in front only where
// Pane may take the front (MayTakeFront). It shows once its Browser has
// joined it with the page (WindowHost::AdoptBrowser, a task after Chrome
// makes the Browser, from inside its constructor), and goes with its last
// Browser, as docking DevTools again closes it. The window keeps itself
// until its widget goes.
WindowHost* MakeDevToolsWindow();

}  // namespace pane

#endif  // PANE_UI_PAGE_DEVTOOLS_WINDOW_H_
