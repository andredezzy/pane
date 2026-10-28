// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_HOOKS_H_
#define PANE_BROWSER_UI_HOOKS_H_

// Function pointers and flags that Pane's patches (native/engine/patches)
// define in Chrome's own files, and that //pane sets as the browser starts
// (pane/browser/ui/hooks.mm). Unset, Chrome behaves as stock. Chrome's files
// include this header (nogncheck), so a signature that drifts on either side
// is a compile error, not a crash. The shape follows Ardia's hooks
// (engine/ardiacore, Apache-2.0), which Pane's window layer is modeled on.

#include <memory>

class Browser;
class BrowserUiController;
class BrowserWebContentsDelegate;
class BrowserWindow;
class BrowserWindowInterface;
class DesktopBrowserWindowCapabilities;
class ExclusiveAccessManager;
class UnloadController;

namespace chrome {
class BrowserCommandController;
}

namespace web_app {
class AppBrowserController;
}

// browser.cc: lets a Browser take Pane's own BrowserWindow (Pane passes it in
// BrowserWindowCreateParams::window, a test-only param otherwise), and asks
// for one when Chrome makes a Browser itself; nullptr keeps Chrome's.
extern BrowserWindow* (*g_pane_browser_window_factory)(Browser* browser);

// browser.cc, unload_controller.cc: a Browser behind one of Pane's windows
// stays open when its last tab closes, unless the window is closing.
extern bool (*g_pane_keeps_window_without_tabs)(
    const BrowserWindowInterface* browser);

// browser_window_features.cc: the WebContentsDelegate of Pane's Browsers,
// which turns popups and new windows into tabs; nullptr keeps Chrome's.
extern std::unique_ptr<BrowserWebContentsDelegate> (
    *g_pane_create_web_contents_delegate)(
    BrowserWindowInterface* browser,
    ExclusiveAccessManager& exclusive_access_manager,
    chrome::BrowserCommandController& command_controller,
    UnloadController& unload_controller,
    web_app::AppBrowserController* app_browser_controller,
    BrowserWindow& window,
    DesktopBrowserWindowCapabilities& capabilities,
    BrowserUiController& browser_ui_controller);

// toast_controller.cc defines it; reading mode and browser_commands.cc read
// it: this process hosts Browsers without a BrowserView, so Chrome's UI that
// needs one (toasts, the reading mode side panel) stands aside for them.
extern bool g_pane_viewless_browsers;

#endif  // PANE_BROWSER_UI_HOOKS_H_
