// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSION_UI_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSION_UI_H_

namespace pane {

// Routes Chrome's extension UI for the app's profiles to the app: the install
// prompt, the "added" bubble, and chrome.action.openPopup().
void InstallExtensionHooks();

}  // namespace pane

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSION_UI_H_
