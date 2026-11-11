// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/ui_hooks.h"

#include <optional>

#include "base/auto_reset.h"
#include "base/no_destructor.h"
#include "chrome/browser/ui/extensions/extension_install_ui.h"
#include "chrome/browser/ui/extensions/reload_page_dialog_controller.h"
#include "pane/browser/extensions/extension_hooks.h"
#include "pane/ui/dialogs/add_extension.h"
#include "pane/ui/dialogs/external_app.h"

namespace pane {

void InstallUIHooks() {
  g_pane_external_protocol_dialog = &AskToOpenExternalApp;
  // Adding an extension asks in Pane's dialog, for Pane's profiles (ADR
  // 0056).
  g_pane_extension_install_dialog = &AskToAddExtension;
  // Chrome's "added" bubble and its "Reload this page to apply your updated
  // settings" have no place in Pane's tabs: the window hears of an install
  // through its Browsers' extensions (BrowserExtensions), and a page takes a
  // change of site access on its next load (ADR 0096).
  static base::NoDestructor<base::AutoReset<bool>> no_install_ui(
      ExtensionInstallUI::disable_ui_for_tests(true));
  static base::NoDestructor<base::AutoReset<std::optional<bool>>>
      no_reload_dialog(
          extensions::ReloadPageDialogController::AcceptDialogForTesting(
              false));
}

}  // namespace pane
