// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/browser/extensions/extensions_internal.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/extensions/api/developer_private/extension_info_generator.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/grit/generated_resources.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/install_prompt_data.h"
#include "extensions/browser/managed_installation_mode.h"
#include "extensions/browser/permissions_manager.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/manifest_url_handlers.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/permissions/permission_message_provider.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/url_pattern.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/app_callback.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/extensions/action_popup.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image.h"

// MARK: - PaneExtension

@implementation PaneExtension

@synthesize identifier = _identifier;
@synthesize name = _name;
@synthesize version = _version;
@synthesize summary = _summary;
@synthesize enabled = _enabled;
@synthesize pinned = _pinned;
@synthesize icon = _icon;
@synthesize optionsURL = _optionsURL;
@synthesize hasAction = _hasAction;
@synthesize actionTitle = _actionTitle;
@synthesize badgeText = _badgeText;
@synthesize badgeBackgroundColor = _badgeBackgroundColor;
@synthesize badgeTextColor = _badgeTextColor;
@synthesize actionEnabled = _actionEnabled;
@synthesize hasPopup = _hasPopup;

- (instancetype)initWithExtension:(const extensions::Extension&)extension
                          profile:(Profile*)profile
                            tabID:(int)tabID
                             icon:(NSImage*)icon {
  if ((self = [super init])) {
    _identifier = base::SysUTF8ToNSString(extension.id());
    _name = base::SysUTF8ToNSString(extension.name());
    _version = base::SysUTF8ToNSString(extension.VersionString());
    const std::string* summary = extension.manifest()->FindStringPath(
        extensions::manifest_keys::kDescription);
    _summary = summary ? base::SysUTF8ToNSString(*summary) : @"";
    _enabled = extensions::ExtensionRegistry::Get(profile)
                   ->enabled_extensions()
                   .Contains(extension.id());
    ToolbarActionsModel* toolbar = ToolbarActionsModel::Get(profile);
    _pinned = toolbar && toolbar->IsActionPinned(extension.id());
    _icon = icon;
    const GURL& options = extensions::OptionsPageInfo::GetOptionsPage(&extension);
    _optionsURL = options.is_valid() ? net::NSURLWithGURL(options) : nil;

    extensions::ExtensionAction* action =
        extensions::ExtensionActionManager::Get(profile)->GetExtensionAction(
            extension);
    _hasAction = action != nullptr;
    _actionTitle = action ? base::SysUTF8ToNSString(action->GetTitle(tabID))
                          : @"";
    _badgeText = action ? base::SysUTF8ToNSString(
                              action->GetDisplayBadgeText(tabID))
                        : @"";
    if (action && _badgeText.length > 0) {
      _badgeBackgroundColor = skia::SkColorToSRGBNSColor(
          action->GetBadgeBackgroundColor(tabID));
      _badgeTextColor =
          skia::SkColorToSRGBNSColor(action->GetBadgeTextColor(tabID));
    }
    _actionEnabled = action && action->GetIsVisible(tabID);
    _hasPopup = action && action->HasPopup(tabID);
  }
  return self;
}

@end

// MARK: - PaneExtensionDetails

namespace {

using extensions::PermissionSet;

std::unique_ptr<const PermissionSet> OrEmpty(
    std::unique_ptr<const PermissionSet> permissions) {
  return permissions ? std::move(permissions)
                     : std::make_unique<PermissionSet>();
}

// The details page's Source, as ExtensionInfoGenerator places the extension
// and item_util.ts words it.
NSString* SourceOf(const extensions::Extension& extension, Profile* profile) {
  using extensions::Manifest;
  extensions::ExtensionManagement* management =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile);
  const extensions::ManagedInstallationMode mode =
      management->GetInstallationMode(&extension);
  const bool from_store = management->UpdatesFromWebstore(extension);
  int source = IDS_EXTENSIONS_INSTALL_LOCATION_UNKNOWN;
  if (Manifest::IsPolicyLocation(extension.location()) ||
      mode == extensions::ManagedInstallationMode::kForced ||
      mode == extensions::ManagedInstallationMode::kRecommended) {
    source = IDS_EXTENSIONS_ITEM_SOURCE_POLICY;
  } else if (extension.location() ==
                 extensions::mojom::ManifestLocation::kInternal &&
             from_store) {
    source = IDS_EXTENSIONS_ITEM_SOURCE_WEBSTORE;
  } else if (Manifest::IsUnpackedLocation(extension.location())) {
    source = IDS_EXTENSIONS_ITEM_SOURCE_UNPACKED;
  } else if (extension.was_installed_by_default() &&
             !extension.was_installed_by_oem() && from_store) {
    source = IDS_EXTENSIONS_ITEM_SOURCE_INSTALLED_BY_DEFAULT;
  } else if (Manifest::IsExternalLocation(extension.location()) && from_store) {
    source = IDS_EXTENSIONS_ITEM_SOURCE_SIDELOADED;
  }
  // "Not from Chrome Web Store." stands in a line here, without its period.
  std::u16string text = l10n_util::GetStringUTF16(source);
  base::TrimString(text, u".", &text);
  return base::SysUTF16ToNSString(text);
}

// The Site access pop-up, as CreateRuntimeHostPermissionsInfo sets it: only
// for an extension whose sites the user can change and that asks for all of
// them (Chrome lists the sites of one that asks for some).
PaneExtensionSiteAccess SiteAccessOf(const extensions::Extension& extension,
                                     Profile* profile) {
  extensions::PermissionsManager* manager =
      extensions::PermissionsManager::Get(profile);
  if (!manager->CanAffectExtension(extension)) {
    return PaneExtensionSiteAccessNone;
  }
  extensions::ExtensionPrefs* prefs = extensions::ExtensionPrefs::Get(profile);
  const bool withheld = manager->HasWithheldHostPermissions(extension);
  std::unique_ptr<const PermissionSet> granted =
      OrEmpty(withheld ? prefs->GetRuntimeGrantedPermissions(extension.id())
                       : prefs->GetGrantedPermissions(extension.id()));
  if (!extension.permissions_data()->withheld_permissions().ShouldWarnAllHosts(
          /*include_api_permissions=*/false) &&
      !granted->ShouldWarnAllHosts(/*include_api_permissions=*/false)) {
    return PaneExtensionSiteAccessNone;
  }
  if (!withheld) {
    return PaneExtensionSiteAccessOnAllSites;
  }
  if (granted->effective_hosts().is_empty()) {
    return PaneExtensionSiteAccessOnClick;
  }
  return granted->ShouldWarnAllHosts(false)
             ? PaneExtensionSiteAccessOnAllSites
             : PaneExtensionSiteAccessOnSpecificSites;
}

// The permission messages of what the extension was granted, as
// AddPermissionsInfo lists them: without its sites when the pop-up shows
// them.
NSArray<NSString*>* PermissionsOf(const extensions::Extension& extension,
                                  Profile* profile,
                                  bool without_sites) {
  std::unique_ptr<const PermissionSet> listed =
      OrEmpty(extensions::ExtensionPrefs::Get(profile)->GetGrantedPermissions(
          extension.id()));
  if (without_sites) {
    listed = std::make_unique<PermissionSet>(
        listed->apis().Clone(), listed->manifest_permissions().Clone(),
        extensions::URLPatternSet(), extensions::URLPatternSet());
  }
  const extensions::PermissionMessageProvider* provider =
      extensions::PermissionMessageProvider::Get();
  NSMutableArray<NSString*>* messages = [NSMutableArray array];
  for (const extensions::PermissionMessage& message :
       provider->GetPermissionMessages(
           provider->GetAllPermissionIDs(*listed, extension.GetType()))) {
    [messages addObject:base::SysUTF16ToNSString(message.message())];
  }
  return [messages copy];
}

// The sites the details page lists under On specific sites: the hosts the
// extension was granted at runtime, none inside another
// (GetSpecificSiteControls), sorted as its getRuntimeHosts_ sorts them.
NSArray<NSString*>* SpecificSitesOf(const extensions::Extension& extension,
                                    Profile* profile) {
  std::unique_ptr<const PermissionSet> granted = OrEmpty(
      extensions::ExtensionPrefs::Get(profile)->GetRuntimeGrantedPermissions(
          extension.id()));
  std::vector<std::string> sites;
  for (const URLPattern& host :
       extensions::ExtensionInfoGenerator::GetDistinctHosts(
           granted->effective_hosts())) {
    sites.push_back(host.GetAsString());
  }
  std::ranges::sort(sites);
  NSMutableArray<NSString*>* list = [NSMutableArray array];
  for (const std::string& site : sites) {
    [list addObject:base::SysUTF8ToNSString(site)];
  }
  return [list copy];
}

NSURL* URLOrNil(const GURL& url) {
  return url.is_valid() ? net::NSURLWithGURL(url) : nil;
}

}  // namespace

@implementation PaneExtensionDetails

@synthesize source = _source;
@synthesize permissions = _permissions;
@synthesize siteAccess = _siteAccess;
@synthesize specificSites = _specificSites;
@synthesize wantsFileAccess = _wantsFileAccess;
@synthesize allowsFileAccess = _allowsFileAccess;
@synthesize homepageURL = _homepageURL;
@synthesize webStoreURL = _webStoreURL;

- (instancetype)initWithExtension:(const extensions::Extension&)extension
                          profile:(Profile*)profile {
  if ((self = [super init])) {
    _source = SourceOf(extension, profile);
    _siteAccess = SiteAccessOf(extension, profile);
    _specificSites = _siteAccess == PaneExtensionSiteAccessOnSpecificSites
                         ? SpecificSitesOf(extension, profile)
                         : @[];
    _permissions = PermissionsOf(extension, profile,
                                 _siteAccess != PaneExtensionSiteAccessNone);
    _wantsFileAccess =
        extension.wants_file_access() ||
        extensions::Manifest::ShouldAlwaysAllowFileAccess(extension.location());
    _allowsFileAccess =
        extensions::util::AllowFileAccess(extension.id(), profile);
    _homepageURL =
        URLOrNil(extensions::ManifestURL::GetManifestHomePageURL(&extension));
    _webStoreURL =
        URLOrNil(extensions::ManifestURL::GetWebStoreURL(&extension));
  }
  return self;
}

@end

// MARK: - PaneExtensionCommand

@implementation PaneExtensionCommand

@synthesize name = _name;
@synthesize summary = _summary;
@synthesize shortcut = _shortcut;

- (instancetype)initWithName:(NSString*)name
                     summary:(NSString*)summary
                    shortcut:(NSString*)shortcut {
  if ((self = [super init])) {
    _name = [name copy];
    _summary = [summary copy];
    _shortcut = [shortcut copy];
  }
  return self;
}

@end

// MARK: - PaneExtensionInstallRequest

@implementation PaneExtensionInstallRequest {
  base::OnceCallback<void(bool)> _answer;
}

@synthesize title = _title;
@synthesize extensionIdentifier = _extensionIdentifier;
@synthesize extensionName = _extensionName;
@synthesize icon = _icon;
@synthesize permissionsHeading = _permissionsHeading;
@synthesize permissions = _permissions;
@synthesize acceptTitle = _acceptTitle;
@synthesize cancelTitle = _cancelTitle;
@synthesize answered = _answered;

- (instancetype)initWithPrompt:(const extensions::InstallPromptData&)prompt
                        answer:(base::OnceCallback<void(bool)>)answer {
  if ((self = [super init])) {
    _title = base::SysUTF16ToNSString(prompt.GetDialogTitle());
    _extensionIdentifier = prompt.extension()
                               ? base::SysUTF8ToNSString(prompt.extension()->id())
                               : @"";
    _extensionName = prompt.extension()
                         ? base::SysUTF8ToNSString(prompt.extension()->name())
                         : @"";
    _icon = prompt.icon().IsEmpty() ? nil : prompt.icon().ToNSImage();
    _permissionsHeading =
        base::SysUTF16ToNSString(prompt.GetPermissionsHeading());
    NSMutableArray<NSString*>* permissions = [NSMutableArray array];
    for (size_t i = 0; i < prompt.GetPermissionCount(); ++i) {
      [permissions addObject:base::SysUTF16ToNSString(prompt.GetPermission(i))];
    }
    _permissions = [permissions copy];
    _acceptTitle = base::SysUTF16ToNSString(prompt.GetAcceptButtonLabel());
    _cancelTitle = base::SysUTF16ToNSString(prompt.GetAbortButtonLabel());
    _answer = std::move(answer);
  }
  return self;
}

- (void)accept {
  [self answer:true name:"PaneExtensionInstallRequest.accept"];
}

- (void)cancel {
  [self answer:false name:"PaneExtensionInstallRequest.cancel"];
}

- (void)answer:(bool)install name:(std::string_view)name {
  if (_answered) {
    return;
  }
  PANE_TRACE("call", name);
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(install);
  }
}

@end

// MARK: - PaneExtensionPopup

@implementation PaneExtensionPopup {
  std::unique_ptr<pane::ActionPopup> _popup;
  NSString* _extensionIdentifier;
  __weak id<PaneExtensionPopupDelegate> _delegate;
  // Kept until closed, whether or not the app holds it.
  PaneExtensionPopup* __strong _keepAlive;
}

- (instancetype)initWithPopup:(std::unique_ptr<pane::ActionPopup>)popup {
  if ((self = [super init])) {
    _popup = std::move(popup);
    _keepAlive = self;
  }
  return self;
}

- (NSString*)extensionIdentifier {
  if (!_extensionIdentifier && _popup && _popup->contents()) {
    _extensionIdentifier = base::SysUTF8ToNSString(
        _popup->contents()->GetLastCommittedURL().host());
  }
  return _extensionIdentifier ?: @"";
}

- (NSView*)nativeView {
  return _popup && _popup->contents()
             ? _popup->contents()->GetNativeView().GetNativeNSView()
             : [[NSView alloc] init];
}

- (NSSize)preferredSize {
  return _popup ? NSMakeSize(_popup->size().width(), _popup->size().height())
                : NSZeroSize;
}

- (BOOL)isOpen {
  return _popup != nullptr;
}

- (id<PaneExtensionPopupDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<PaneExtensionPopupDelegate>)delegate {
  _delegate = delegate;
}

- (void)close {
  if (!_popup) {
    return;
  }
  PANE_TRACE("call", "PaneExtensionPopup.close");
  (void)self.extensionIdentifier;
  _popup.reset();
  _keepAlive = nil;
}

- (void)popupDidResize:(NSSize)size {
  if ([_delegate respondsToSelector:@selector(extensionPopup:didResize:)]) {
    [_delegate extensionPopup:self didResize:size];
  }
}

- (void)popupDidClose {
  if (!_popup) {
    return;
  }
  [self notifyDidClose];
  // The host is mid-close: let it return first.
  dispatch_async(dispatch_get_main_queue(), ^{
    [self close];
  });
}

- (void)browserDidClose {
  if (!_popup) {
    return;
  }
  [self notifyDidClose];
  [self close];
}

- (void)notifyDidClose {
  PANE_TRACE("callback", "PaneExtensionPopupDelegate.didClose");
  (void)self.extensionIdentifier;
  if ([_delegate respondsToSelector:@selector(extensionPopupDidClose:)]) {
    pane::ScopedAppCallback callback;
    [_delegate extensionPopupDidClose:self];
  }
}

@end
