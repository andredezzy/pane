// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/browser/extensions/extensions_internal.h"

#include <utility>

#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/install_prompt_data.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/app_callback.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/extensions/action_popup.h"
#include "skia/ext/skia_utils_mac.h"
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
  pane::Trace("callback", "PaneExtensionPopupDelegate.didClose");
  (void)self.extensionIdentifier;
  if ([_delegate respondsToSelector:@selector(extensionPopupDidClose:)]) {
    [_delegate extensionPopupDidClose:self];
  }
  // The host is mid-close: let it return first.
  dispatch_async(dispatch_get_main_queue(), ^{
    [self close];
  });
}

@end
