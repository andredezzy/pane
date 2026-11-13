// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/add_extension.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_install_prompt_show_params.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/install_prompt_data.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/model/extension_text.h"
#include "pane/ui/app/ui_app.h"
#include "pane/ui/dialogs/extension_prompt_view.h"
#include "pane/ui/extensions/extensions_ui.h"

namespace pane {

bool AskToAddExtension(
    std::unique_ptr<ExtensionInstallPromptShowParams>& show_params,
    ExtensionInstallPrompt::DoneCallback& done_callback,
    std::unique_ptr<extensions::InstallPromptData>& prompt) {
  UiApp* app = UiApp::Get();
  Profile* profile = show_params && show_params->profile()
                         ? show_params->profile()->GetOriginalProfile()
                         : nullptr;
  if (!app || !profile || !prompt ||
      !app->Extensions().IsPaneProfile(*profile)) {
    return false;
  }
  const extensions::Extension* extension = prompt->extension();
  const std::string id = extension ? extension->id() : std::string();
  const std::string name = extension ? extension->name() : std::string();
  const std::string accept = base::UTF16ToUTF8(prompt->GetAcceptButtonLabel());

  ExtensionPrompt request;
  request.kind = ExtensionPrompt::Kind::kInstall;
  request.extension_id = id;
  request.title = base::UTF8ToUTF16(model::ExtensionPromptTitle(
      accept, name, base::UTF16ToUTF8(prompt->GetDialogTitle())));
  request.icon = prompt->icon();
  request.permissions_heading = prompt->GetPermissionsHeading();
  for (size_t index = 0; index < prompt->GetPermissionCount(); ++index) {
    request.permissions.push_back(prompt->GetPermission(index));
  }
  request.accept_title =
      base::UTF8ToUTF16(model::ExtensionPromptAcceptTitle(accept));
  request.cancel_title = prompt->GetAbortButtonLabel();
  request.answer = base::BindOnce(
      [](ExtensionInstallPrompt::DoneCallback done, bool accepted,
         bool report_abuse) {
        PANE_TRACE("call", accepted ? "PaneExtensionInstallRequest.accept"
                                    : "PaneExtensionInstallRequest.cancel");
        using Result = ExtensionInstallPrompt::Result;
        std::move(done).Run(ExtensionInstallPrompt::DoneCallbackPayload(
            accepted ? Result::ACCEPTED : Result::USER_CANCELED));
      },
      std::move(done_callback));
  app->Extensions().Ask(std::move(request));
  return true;
}

}  // namespace pane
