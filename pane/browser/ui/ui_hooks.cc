// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/ui_hooks.h"

#include "pane/browser/extensions/extension_ui.h"
#include "pane/ui/dialogs/external_app.h"

namespace pane {

void InstallUIHooks() {
  g_pane_external_protocol_dialog = &AskToOpenExternalApp;
  InstallExtensionHooks();
}

}  // namespace pane
