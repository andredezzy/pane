// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_ADD_EXTENSION_H_
#define PANE_UI_DIALOGS_ADD_EXTENSION_H_

#include <memory>

#include "chrome/browser/extensions/extension_install_prompt.h"

class ExtensionInstallPromptShowParams;

namespace extensions {
class InstallPromptData;
}  // namespace extensions

namespace pane {

// Asks before an extension is added to one of Pane's profiles, in Dia's
// dialog over the window (ADR 0056), with Chrome's own prompt's words: "Add
// “<name>” to Pane?" for an install, Chrome's titles for its other prompts
// (new permissions, re-enabling), what the extension can do, and Cancel and
// the accept, as Chrome's dialog would have asked. Chrome's hook for its
// dialog (patch 0005's g_pane_extension_install_dialog) calls it, Chromium
// having no view factory for that dialog; false for a profile that isn't
// one of Pane's, which keeps Chrome's dialog. Accepted, Chrome installs it;
// otherwise nothing is installed.
bool AskToAddExtension(
    std::unique_ptr<ExtensionInstallPromptShowParams>& show_params,
    ExtensionInstallPrompt::DoneCallback& done_callback,
    std::unique_ptr<extensions::InstallPromptData>& prompt);

}  // namespace pane

#endif  // PANE_UI_DIALOGS_ADD_EXTENSION_H_
