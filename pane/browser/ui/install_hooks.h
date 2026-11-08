// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_INSTALL_HOOKS_H_
#define PANE_BROWSER_UI_INSTALL_HOOKS_H_

namespace pane {

// Sets the hooks of pane/browser/ui/hooks.h. Called once, before the first
// Browser exists.
void InstallBrowserHooks();

// Routes Chrome's dialogs and bubbles for Pane's tabs to Pane's UI
// (ui_hooks.mm). Runs as the browser starts, before any tab exists.
void InstallUIHooks();

}  // namespace pane

#endif  // PANE_BROWSER_UI_INSTALL_HOOKS_H_
