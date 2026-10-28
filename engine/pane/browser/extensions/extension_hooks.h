// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSION_HOOKS_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSION_HOOKS_H_

// Hooks for Chrome's extension UI (patch 0005-pane-extensions). Like
// ui_hooks.h: Chrome's files define them and include this header (nogncheck);
// //pane sets them as the browser starts (extension_ui.mm). They only take
// over for profiles the app has open.

#include <memory>
#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"

class BrowserWindowInterface;
class ExtensionInstallPromptShowParams;
class Profile;
class SkBitmap;

namespace extensions {
class Extension;
class InstallPromptData;
}  // namespace extensions

// extension_install_dialog_view.cc: "Add <extension>?". True when the app
// asks instead of Chrome's dialog; the arguments are then consumed.
extern bool (*g_pane_extension_install_dialog)(
    std::unique_ptr<ExtensionInstallPromptShowParams>& show_params,
    ExtensionInstallPrompt::DoneCallback& done_callback,
    std::unique_ptr<extensions::InstallPromptData>& prompt);

// extension_install_ui_desktop.cc: an extension was installed. True when the
// app was told, instead of Chrome's "added" bubble.
extern bool (*g_pane_extension_installed)(
    Profile* profile,
    scoped_refptr<const extensions::Extension> extension,
    const SkBitmap* icon);

// extension_action_api.cc: chrome.action.openPopup() for a Browser Pane
// hosts. Empty: not Pane's; true: shown (`callback` runs once it is); false:
// refused, with `error`.
extern std::optional<bool> (*g_pane_open_action_popup)(
    BrowserWindowInterface& browser,
    const extensions::Extension& extension,
    ShowPopupCallback& callback,
    std::string* error);

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSION_HOOKS_H_
