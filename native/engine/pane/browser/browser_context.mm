// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserContext.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "base/functional/bind.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "components/crx_file/id_util.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/uninstall_reason.h"
#include "pane/browser/downloads.h"
#include "pane/browser/extensions/action_popup.h"
#include "pane/browser/extensions/extensions_bridge.h"
#include "pane/browser/ui/window_host.h"
#include "pane/browser/engine_trace.h"

#import "pane/browser/browser_context_internal.h"
#import "pane/public/PaneBrowserWindow.h"
#import "pane/public/PaneExtensions.h"

@implementation PaneBrowserContext {
  NSString* _identifier;
  raw_ptr<Profile> _profile;
  __weak id<PaneBrowserContextDelegate> _delegate;
  // A Pane context counts as an open window for its profile, so Chrome does
  // not unload the profile while the app shows it.
  std::unique_ptr<ScopedProfileKeepAlive> _keepAlive;
  std::unique_ptr<pane::DownloadsBridge> _downloads;
  std::unique_ptr<pane::ExtensionsBridge> _extensions;
}

- (instancetype)initWithProfile:(Profile*)profile
                     identifier:(NSString*)identifier {
  if ((self = [super init])) {
    _identifier = [identifier copy];
    _profile = profile;
    _keepAlive = std::make_unique<ScopedProfileKeepAlive>(
        profile, ProfileKeepAliveOrigin::kBrowserWindow);
    _downloads = std::make_unique<pane::DownloadsBridge>(self, profile);
    _extensions = std::make_unique<pane::ExtensionsBridge>(self, profile);
    PrefService* prefs = profile->GetPrefs();
    // Downloads go straight to the download directory, never to a dialog.
    prefs->SetBoolean(prefs::kPromptForDownload, false);
    // Chrome's password manager and autofill stay off: Pane relies on
    // extension password managers, so their bubbles never appear.
    prefs->SetBoolean(password_manager::prefs::kCredentialsEnableService,
                      false);
    prefs->SetBoolean(password_manager::prefs::kCredentialsEnableAutosignin,
                      false);
    prefs->SetBoolean(autofill::prefs::kAutofillProfileEnabled, false);
    prefs->SetBoolean(autofill::prefs::kAutofillCreditCardEnabled, false);
  }
  return self;
}

- (NSString*)identifier {
  return _identifier;
}

- (Profile*)profile {
  return _profile;
}

- (id<PaneBrowserContextDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<PaneBrowserContextDelegate>)delegate {
  _delegate = delegate;
}

- (NSURL*)downloadDirectory {
  if (!_profile) {
    return [NSURL fileURLWithPath:NSTemporaryDirectory()];
  }
  base::FilePath path =
      DownloadPrefs::FromBrowserContext(_profile)->DownloadPath();
  return [NSURL fileURLWithPath:base::SysUTF8ToNSString(path.value())
                    isDirectory:YES];
}

- (void)setDownloadDirectory:(NSURL*)downloadDirectory {
  pane::Trace("call", "PaneBrowserContext.setDownloadDirectory",
              base::DictValue().Set(
                  "path", base::SysNSStringToUTF8(downloadDirectory.path)));
  if (_profile) {
    DownloadPrefs::FromBrowserContext(_profile)->SetDownloadPath(
        base::FilePath(base::SysNSStringToUTF8(downloadDirectory.path)));
  }
}

- (void)shutDown {
  _extensions.reset();
  _downloads.reset();
  _keepAlive.reset();
  _profile = nullptr;
}

// MARK: Extensions

- (NSArray<PaneExtension*>*)extensions {
  return _extensions ? _extensions->Snapshot() : @[];
}

- (const extensions::Extension*)installedExtension:(NSString*)identifier {
  return _profile ? extensions::ExtensionRegistry::Get(_profile)
                        ->GetInstalledExtension(
                            base::SysNSStringToUTF8(identifier))
                  : nullptr;
}

- (void)installExtensionWithIdentifier:(NSString*)identifier
                                window:(PaneBrowserWindow*)window {
  const std::string id = base::SysNSStringToUTF8(identifier);
  pane::Trace("call", "PaneBrowserContext.installExtension",
              base::DictValue().Set("extension", id));
  if (!_profile || !crx_file::id_util::IdIsValid(id)) {
    return;
  }
  auto installer = base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
      id, _profile.get(), gfx::NativeWindow(window.window),
      base::BindOnce([](const std::string& id, bool success,
                        const std::string& error,
                        extensions::webstore_install::Result result) {
        pane::Trace("callback", "PaneBrowserContext.installExtensionDone",
                    base::DictValue()
                        .Set("extension", id)
                        .Set("success", success)
                        .Set("error", error));
      }, id));
  installer->BeginInstall();
}

- (void)setEnabled:(BOOL)enabled forExtension:(NSString*)identifier {
  pane::Trace("call", "PaneBrowserContext.setExtensionEnabled",
              base::DictValue()
                  .Set("extension", base::SysNSStringToUTF8(identifier))
                  .Set("enabled", static_cast<bool>(enabled)));
  if (![self installedExtension:identifier]) {
    return;
  }
  auto* registrar = extensions::ExtensionRegistrar::Get(_profile);
  const std::string id = base::SysNSStringToUTF8(identifier);
  if (enabled) {
    registrar->EnableExtension(id);
  } else {
    registrar->DisableExtension(
        id, {extensions::disable_reason::DISABLE_USER_ACTION});
  }
}

- (void)setPinned:(BOOL)pinned forExtension:(NSString*)identifier {
  pane::Trace("call", "PaneBrowserContext.setExtensionPinned",
              base::DictValue()
                  .Set("extension", base::SysNSStringToUTF8(identifier))
                  .Set("pinned", static_cast<bool>(pinned)));
  if (ToolbarActionsModel* toolbar =
          _profile ? ToolbarActionsModel::Get(_profile) : nullptr) {
    toolbar->SetActionVisibility(base::SysNSStringToUTF8(identifier), pinned);
  }
}

- (void)uninstallExtension:(NSString*)identifier {
  pane::Trace("call", "PaneBrowserContext.uninstallExtension",
              base::DictValue().Set("extension",
                                    base::SysNSStringToUTF8(identifier)));
  if (![self installedExtension:identifier]) {
    return;
  }
  std::u16string error;
  extensions::ExtensionRegistrar::Get(_profile)->UninstallExtension(
      base::SysNSStringToUTF8(identifier),
      extensions::UNINSTALL_REASON_USER_INITIATED, &error);
}

- (void)updateExtensions {
  pane::Trace("call", "PaneBrowserContext.updateExtensions");
  if (auto* updater =
          _profile ? extensions::ExtensionUpdater::Get(_profile) : nullptr) {
    updater->CheckNow(extensions::ExtensionUpdater::CheckParams());
  }
}

- (void)runActionOfExtension:(NSString*)identifier
                    inWindow:(PaneBrowserWindow*)window {
  pane::Trace("call", "PaneBrowserContext.runAction",
              base::DictValue().Set("extension",
                                    base::SysNSStringToUTF8(identifier)));
  if (!_profile) {
    return;
  }
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(_profile)
          ->enabled_extensions()
          .GetByID(base::SysNSStringToUTF8(identifier));
  pane::WindowHost* host = pane::WindowHost::ForNSWindow(window.window);
  Browser* browser = host && extension ? host->BrowserFor(_profile) : nullptr;
  content::WebContents* contents =
      browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
  auto* runner = contents
                     ? extensions::ExtensionActionRunner::GetForWebContents(
                           contents)
                     : nullptr;
  if (!runner) {
    return;
  }
  if (runner->RunAction(extension, /*grant_tab_permissions=*/true) ==
      extensions::ExtensionAction::ShowAction::kShowPopup) {
    pane::ActionPopup::Show(browser, *extension, ShowPopupCallback());
  }
}

@end
