// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_SELECT_FILE_DIALOG_H_
#define PANE_BROWSER_UI_SELECT_FILE_DIALOG_H_

namespace pane {

// Routes every file dialog Chrome opens for one of Pane's windows (file
// inputs, File System Access pickers, Save As) to the window's delegate
// (browserWindow:runFileChooser:); dialogs for other windows stay Chrome's
// own NSOpenPanel/NSSavePanel. Installed once, as the engine starts.
void InstallSelectFileDialogFactory();

}  // namespace pane

#endif  // PANE_BROWSER_UI_SELECT_FILE_DIALOG_H_
