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
#include "base/strings/string_util.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "components/language/core/browser/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "components/proxy_config/proxy_prefs.h"
#include "base/functional/bind.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/crx_file/id_util.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/path_util.h"
#include "extensions/browser/permissions/scripting_permissions_modifier.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/uninstall_reason.h"
#include "pane/browser/downloads.h"
#include "pane/browser/fingerprints.h"
#include "pane/browser/extensions/action_popup.h"
#include "pane/browser/extensions/extension_menu.h"
#include "pane/browser/extensions/extension_shortcuts.h"
#include "pane/browser/extensions/extensions_bridge.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/window_host.h"
#include "extensions/common/command.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/cocoa/cocoa_event_utils.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "pane/browser/engine_trace.h"

#import "pane/browser/browser_context_internal.h"
#import "pane/browser/extensions/extensions_internal.h"
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
    // No browser sign-in (its account list goes to accounts.google.com with
    // the profile's cookies) and no Translate (its ranker model downloads
    // from gstatic): Pane has neither, as the Electron app had neither.
    // Signing in to Google's websites is unaffected.
    prefs->SetBoolean(prefs::kSigninAllowed, false);
    prefs->SetBoolean(translate::prefs::kOfferTranslateEnabled, false);
    [self applyFingerprint];
  }
  return self;
}

- (void)applyFingerprint {
  if (!_profile) {
    return;
  }

  // Chrome derives the Accept-Language list from the user's selected
  // languages (LanguagePrefs), so both are set.
  PrefService* prefs = _profile->GetPrefs();
  const pane::Fingerprint* fingerprint =
      pane::Fingerprints::Get().For(_profile);
  if (fingerprint && !fingerprint->languages.empty()) {
    const std::string languages = base::JoinString(fingerprint->languages, ",");
    prefs->SetString(language::prefs::kSelectedLanguages, languages);
    prefs->SetString(language::prefs::kAcceptLanguages, languages);
  } else {
    language::ResetLanguagePrefs(prefs);
  }
  PANE_TRACE("engine", "PaneBrowserContext.applyFingerprint",
             base::DictValue()
                 .Set("identifier", base::SysNSStringToUTF8(_identifier))
                 .Set("acceptLanguages",
                      prefs->GetString(language::prefs::kAcceptLanguages)));
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
  PANE_TRACE("call", "PaneBrowserContext.setDownloadDirectory",
             base::DictValue().Set(
                 "path", base::SysNSStringToUTF8(downloadDirectory.path)));
  if (_profile) {
    DownloadPrefs::FromBrowserContext(_profile)->SetDownloadPath(
        base::FilePath(base::SysNSStringToUTF8(downloadDirectory.path)));
  }
}

// MARK: Network

- (NSString*)proxyRules {
  if (!_profile) {
    return nil;
  }
  ProxyConfigDictionary config(
      _profile->GetPrefs()->GetDict(proxy_config::prefs::kProxy).Clone());
  std::string server;
  ProxyPrefs::ProxyMode mode;
  if (!config.GetMode(&mode) || mode != ProxyPrefs::MODE_FIXED_SERVERS ||
      !config.GetProxyServer(&server)) {
    return nil;
  }
  return base::SysUTF8ToNSString(server);
}

- (void)setProxyRules:(NSString*)proxyRules {
  PANE_TRACE("call", "PaneBrowserContext.setProxyRules",
             base::DictValue().Set(
                 "rules", proxyRules ? base::SysNSStringToUTF8(proxyRules)
                                     : std::string()));
  if (!_profile) {
    return;
  }
  PrefService* prefs = _profile->GetPrefs();
  if (proxyRules.length) {
    prefs->SetDict(proxy_config::prefs::kProxy,
                   ProxyConfigDictionary::CreateFixedServers(
                       base::SysNSStringToUTF8(proxyRules), std::string()));
  } else {
    prefs->ClearPref(proxy_config::prefs::kProxy);
  }
}

- (NSString*)webRTCIPHandlingPolicy {
  return _profile ? base::SysUTF8ToNSString(_profile->GetPrefs()->GetString(
                        prefs::kWebRTCIPHandlingPolicy))
                  : @"default";
}

- (void)setWebRTCIPHandlingPolicy:(NSString*)policy {
  PANE_TRACE("call", "PaneBrowserContext.setWebRTCIPHandlingPolicy",
             base::DictValue().Set("policy", base::SysNSStringToUTF8(policy)));
  if (_profile) {
    _profile->GetPrefs()->SetString(prefs::kWebRTCIPHandlingPolicy,
                                    base::SysNSStringToUTF8(policy));
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
  PANE_TRACE("call", "PaneBrowserContext.installExtension",
             base::DictValue().Set("extension", id));
  if (!_profile || !crx_file::id_util::IdIsValid(id)) {
    return;
  }
  auto installer = base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
      id, _profile.get(), gfx::NativeWindow(window.window),
      base::BindOnce(
          [](const std::string& id, bool success, const std::string& error,
             extensions::webstore_install::Result result) {
            PANE_TRACE("callback", "PaneBrowserContext.installExtensionDone",
                       base::DictValue()
                           .Set("extension", id)
                           .Set("success", success)
                           .Set("error", error));
          },
          id));
  installer->BeginInstall();
}

- (void)setEnabled:(BOOL)enabled forExtension:(NSString*)identifier {
  PANE_TRACE("call", "PaneBrowserContext.setExtensionEnabled",
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
  PANE_TRACE("call", "PaneBrowserContext.setExtensionPinned",
             base::DictValue()
                 .Set("extension", base::SysNSStringToUTF8(identifier))
                 .Set("pinned", static_cast<bool>(pinned)));
  if (ToolbarActionsModel* toolbar =
          _profile ? ToolbarActionsModel::Get(_profile) : nullptr) {
    toolbar->SetActionVisibility(base::SysNSStringToUTF8(identifier), pinned);
  }
}

- (void)uninstallExtension:(NSString*)identifier {
  PANE_TRACE(
      "call", "PaneBrowserContext.uninstallExtension",
      base::DictValue().Set("extension", base::SysNSStringToUTF8(identifier)));
  if (![self installedExtension:identifier]) {
    return;
  }
  std::u16string error;
  extensions::ExtensionRegistrar::Get(_profile)->UninstallExtension(
      base::SysNSStringToUTF8(identifier),
      extensions::UNINSTALL_REASON_USER_INITIATED, &error);
}

- (void)runActionOfExtension:(NSString*)identifier
                    inWindow:(PaneBrowserWindow*)window {
  PANE_TRACE(
      "call", "PaneBrowserContext.runAction",
      base::DictValue().Set("extension", base::SysNSStringToUTF8(identifier)));
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

// The extension shortcuts of this profile's Browser in `window`, once it has
// one.
- (pane::ExtensionShortcuts*)extensionShortcutsInWindow:
    (PaneBrowserWindow*)window {
  pane::WindowHost* host = pane::WindowHost::ForNSWindow(window.window);
  Browser* browser = host && _profile ? host->ExistingBrowserFor(_profile)
                                      : nullptr;
  pane::HostedBrowserWindow* hosted =
      browser ? pane::HostedBrowserWindow::FromWindow(
                    static_cast<BrowserWindow*>(browser->GetWindow()))
              : nullptr;
  return hosted ? hosted->extension_shortcuts() : nullptr;
}

- (BOOL)runExtensionShortcut:(NSEvent*)event
                    inWindow:(PaneBrowserWindow*)window {
  if (event.type != NSEventTypeKeyDown) {
    return NO;
  }
  pane::ExtensionShortcuts* shortcuts =
      [self extensionShortcutsInWindow:window];
  const ui::Accelerator accelerator(
      ui::KeyboardCodeFromNSEvent(event),
      ui::EventFlagsFromModifiers(event.modifierFlags));
  extensions::ExtensionId extension;
  std::string command;
  if (!shortcuts || shortcuts->shortcut_handling_suspended() ||
      !shortcuts->Find(accelerator, &extension, &command) ||
      pane::MenuHasKeyEquivalent(NSApp.mainMenu, event)) {
    return NO;
  }
  PANE_TRACE("call", "PaneBrowserContext.runExtensionShortcut",
             base::DictValue().Set("extension", extension).Set("command",
                                                               command));
  if (extensions::Command::IsActionRelatedCommand(command)) {
    [self runActionOfExtension:base::SysUTF8ToNSString(extension)
                      inWindow:window];
    return YES;
  }
  return shortcuts->Notify(accelerator);
}

- (NSArray<PaneExtensionCommand*>*)commandsOfExtension:(NSString*)identifier {
  const extensions::Extension* extension = [self installedExtension:identifier];
  if (!extension) {
    return @[];
  }
  NSMutableArray<PaneExtensionCommand*>* commands = [NSMutableArray array];
  for (const ui::Command& command : pane::CommandsOf(_profile, *extension)) {
    [commands
        addObject:[[PaneExtensionCommand alloc]
                      initWithName:base::SysUTF8ToNSString(
                                       command.command_name())
                           summary:base::SysUTF16ToNSString(
                                       command.description())
                          shortcut:base::SysUTF8ToNSString(
                                       extensions::Command::AcceleratorToString(
                                           command.accelerator()))]];
  }
  return commands;
}

- (NSString*)setShortcut:(NSString*)shortcut
               ofCommand:(NSString*)command
               extension:(NSString*)identifier {
  PANE_TRACE("call", "PaneBrowserContext.setExtensionShortcut",
             base::DictValue()
                 .Set("extension", base::SysNSStringToUTF8(identifier))
                 .Set("command", base::SysNSStringToUTF8(command))
                 .Set("shortcut", base::SysNSStringToUTF8(shortcut)));
  if (![self installedExtension:identifier]) {
    return nil;
  }
  const extensions::ExtensionId moved = pane::SetShortcut(
      _profile, base::SysNSStringToUTF8(identifier),
      base::SysNSStringToUTF8(command), base::SysNSStringToUTF8(shortcut));
  return moved.empty() ? nil : base::SysUTF8ToNSString(moved);
}

- (void)setExtensionShortcutsSuspended:(BOOL)suspended
                              inWindow:(PaneBrowserWindow*)window {
  PANE_TRACE("call", "PaneBrowserContext.setExtensionShortcutsSuspended",
             base::DictValue().Set("suspended", static_cast<bool>(suspended)));
  if (pane::ExtensionShortcuts* shortcuts =
          [self extensionShortcutsInWindow:window]) {
    shortcuts->SetShortcutHandlingSuspended(suspended);
  }
}

// As Chrome's shortcut field writes a key (cr_shortcut_util.ts), with
// Chrome's own conversions, and only what Chrome's parse takes back.
+ (NSString*)extensionShortcutForEvent:(NSEvent*)event {
  if (event.type != NSEventTypeKeyDown) {
    return nil;
  }
  const std::string shortcut =
      extensions::Command::AcceleratorToString(ui::Accelerator(
          ui::KeyboardCodeFromNSEvent(event),
          ui::EventFlagsFromModifiers(event.modifierFlags)));
  if (shortcut.empty() ||
      extensions::Command::StringToAccelerator(shortcut, std::string())
              .key_code() == ui::VKEY_UNKNOWN) {
    return nil;
  }
  return base::SysUTF8ToNSString(shortcut);
}

- (PaneExtensionActionMenu*)actionMenuOfExtension:(NSString*)identifier
                                         inWindow:(PaneBrowserWindow*)window {
  if (!_profile) {
    return nil;
  }
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(_profile)
          ->enabled_extensions()
          .GetByID(base::SysNSStringToUTF8(identifier));
  pane::WindowHost* host = pane::WindowHost::ForNSWindow(window.window);
  Browser* browser = host && extension ? host->BrowserFor(_profile) : nullptr;
  content::WebContents* contents =
      browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
  if (!contents) {
    return nil;
  }
  PaneExtensionActionMenu* menu = [[PaneExtensionActionMenu alloc]
      initWithMenu:pane::ExtensionMenu::ForAction(_profile, *extension)
         extension:*extension
          contents:*contents];
  return menu.items.count || menu.siteAccessItems.count ? menu : nil;
}

- (PaneExtensionDetails*)detailsOfExtension:(NSString*)identifier {
  const extensions::Extension* extension = [self installedExtension:identifier];
  return extension ? [[PaneExtensionDetails alloc] initWithExtension:*extension
                                                             profile:_profile]
                   : nil;
}

- (void)sizeOfExtension:(NSString*)identifier
             completion:(void (^)(NSString*))completion {
  const extensions::Extension* extension = [self installedExtension:identifier];
  if (!extension) {
    completion(nil);
    return;
  }
  // As chrome://extensions' details page measures it.
  extensions::path_util::CalculateAndFormatExtensionDirectorySize(
      extension->path(), IDS_APPLICATION_INFO_SIZE_SMALL_LABEL,
      base::BindOnce(^(const std::u16string& size) {
        completion(base::SysUTF16ToNSString(size));
      }));
}

// As developerPrivate.updateExtensionConfiguration sets hostAccess.
- (void)setSiteAccess:(PaneExtensionSiteAccess)access
         forExtension:(NSString*)identifier {
  PANE_TRACE("call", "PaneBrowserContext.setExtensionSiteAccess",
             base::DictValue()
                 .Set("extension", base::SysNSStringToUTF8(identifier))
                 .Set("access", static_cast<int>(access)));
  const extensions::Extension* extension = [self installedExtension:identifier];
  extensions::PermissionsManager* manager =
      _profile ? extensions::PermissionsManager::Get(_profile) : nullptr;
  if (!extension || !manager->CanAffectExtension(*extension)) {
    return;
  }
  extensions::ScriptingPermissionsModifier modifier(_profile, extension);
  switch (access) {
    case PaneExtensionSiteAccessOnClick:
      modifier.SetWithholdHostPermissions(true);
      modifier.RemoveAllGrantedHostPermissions();
      break;
    case PaneExtensionSiteAccessOnSpecificSites:
      if (manager->HasBroadGrantedHostPermissions(*extension)) {
        modifier.RemoveBroadGrantedHostPermissions();
      }
      modifier.SetWithholdHostPermissions(true);
      break;
    case PaneExtensionSiteAccessOnAllSites:
      modifier.SetWithholdHostPermissions(false);
      break;
    case PaneExtensionSiteAccessNone:
      break;
  }
}

- (void)setAllowsFileAccess:(BOOL)allows forExtension:(NSString*)identifier {
  PANE_TRACE("call", "PaneBrowserContext.setExtensionFileAccess",
             base::DictValue()
                 .Set("extension", base::SysNSStringToUTF8(identifier))
                 .Set("allows", static_cast<bool>(allows)));
  if ([self installedExtension:identifier]) {
    extensions::util::SetAllowFileAccess(base::SysNSStringToUTF8(identifier),
                                         _profile, allows);
  }
}

@end
