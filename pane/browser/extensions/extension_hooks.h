// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSION_HOOKS_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSION_HOOKS_H_

// The hook for Chrome's extension install prompt (patch 0005-pane-extensions).
// Like ui_hooks.h: Chrome's file defines it and includes this header
// (nogncheck); //pane sets it as the browser starts (extension_ui.mm). It
// only takes over for profiles the app has open. chrome.action.openPopup
// needs no hook: Pane registers an ExtensionsContainer for each Browser it
// hosts (pane/ui/extensions/browser_extensions.h).

#include <memory>

#include "chrome/browser/extensions/extension_install_prompt.h"

class ExtensionInstallPromptShowParams;

namespace extensions {
class InstallPromptData;
}  // namespace extensions

// extension_install_dialog_view.cc: "Add <extension>?". True when the app
// asks instead of Chrome's dialog; the arguments are then consumed.
extern bool (*g_pane_extension_install_dialog)(
    std::unique_ptr<ExtensionInstallPromptShowParams>& show_params,
    ExtensionInstallPrompt::DoneCallback& done_callback,
    std::unique_ptr<extensions::InstallPromptData>& prompt);

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSION_HOOKS_H_
