// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_DEVTOOLS_WINDOW_HOST_H_
#define PANE_BROWSER_UI_DEVTOOLS_WINDOW_HOST_H_

class Browser;
class BrowserWindow;

namespace pane {

// The window of undocked DevTools: a window of Pane's (the same Views window
// as the app's) showing only the DevTools page, instead of Chrome's DevTools
// window. Made from inside Browser's constructor, so the Browser joins it a
// task later, once built. Its NSWindow's identifier is "pane.devtools".
BrowserWindow* CreateDevToolsWindow(Browser* browser);

}  // namespace pane

#endif  // PANE_BROWSER_UI_DEVTOOLS_WINDOW_HOST_H_
