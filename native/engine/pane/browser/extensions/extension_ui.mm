// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/extensions/extension_ui.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/extensions/extension_install_prompt_show_params.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/install_prompt_data.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/extensions/action_popup.h"
#include "pane/browser/extensions/extension_hooks.h"
#include "pane/browser/ui/browser_window.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"

#import "pane/browser/browser_application_internal.h"
#import "pane/browser/extensions/extensions_internal.h"
#import "pane/public/PaneBrowserContext.h"

namespace pane {

namespace {

PaneBrowserContext* ContextFor(Profile* profile) {
  return profile ? [PaneBrowserApplication.sharedApplication
                       contextForProfile:profile->GetOriginalProfile()]
                 : nil;
}

bool ConfirmInstall(
    std::unique_ptr<ExtensionInstallPromptShowParams>& show_params,
    ExtensionInstallPrompt::DoneCallback& done_callback,
    std::unique_ptr<extensions::InstallPromptData>& prompt) {
  PaneBrowserContext* context = ContextFor(show_params->profile());
  id<PaneBrowserContextDelegate> delegate = context.delegate;
  if (![delegate respondsToSelector:@selector(browserContext:
                                       confirmExtensionInstall:)]) {
    return false;
  }
  PaneExtensionInstallRequest* request = [[PaneExtensionInstallRequest alloc]
      initWithPrompt:*prompt
              answer:base::BindOnce(
                         [](ExtensionInstallPrompt::DoneCallback done,
                            bool install) {
                           using Result = ExtensionInstallPrompt::Result;
                           std::move(done).Run(
                               ExtensionInstallPrompt::DoneCallbackPayload(
                                   install ? Result::ACCEPTED
                                           : Result::USER_CANCELED));
                         },
                         std::move(done_callback))];
  Trace("callback", "PaneBrowserContextDelegate.confirmExtensionInstall",
        base::DictValue().Set(
            "extension", prompt->extension() ? prompt->extension()->id() : ""));
  [delegate browserContext:context confirmExtensionInstall:request];
  return true;
}

bool Installed(Profile* profile,
               scoped_refptr<const extensions::Extension> extension,
               const SkBitmap* icon) {
  PaneBrowserContext* context = ContextFor(profile);
  if (!context) {
    return false;
  }
  Trace("callback", "PaneBrowserContextDelegate.didInstallExtension",
        base::DictValue().Set("extension", extension->id()));
  id<PaneBrowserContextDelegate> delegate = context.delegate;
  if ([delegate respondsToSelector:@selector(browserContext:
                                      didInstallExtension:)]) {
    // The registry may not list it yet: the snapshot comes from Chrome's own.
    PaneExtension* installed = [[PaneExtension alloc]
        initWithExtension:*extension
                  profile:profile->GetOriginalProfile()
                    tabID:extensions::ExtensionAction::kDefaultTabId
                     icon:icon && !icon->isNull()
                              ? skia::SkBitmapToNSImage(*icon)
                              : nil];
    [delegate browserContext:context didInstallExtension:installed];
  }
  return true;
}

std::optional<bool> OpenActionPopup(BrowserWindowInterface& browser,
                                    const extensions::Extension& extension,
                                    ShowPopupCallback& callback,
                                    std::string* error) {
  if (!HostedBrowserWindow::IsOurs(&browser)) {
    return std::nullopt;
  }
  ActionPopup::Show(static_cast<Browser*>(&browser), extension,
                    std::move(callback));
  return true;
}

}  // namespace

void InstallExtensionHooks() {
  g_pane_extension_install_dialog = &ConfirmInstall;
  g_pane_extension_installed = &Installed;
  g_pane_open_action_popup = &OpenActionPopup;
}

}  // namespace pane
