// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEEXTENSIONS_H_
#define PANE_PUBLIC_PANEEXTENSIONS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

// Chrome extensions, per profile (PaneBrowserContext): Chrome's extension
// system runs them; the app shows their install prompts and management (X3
// to X5 of docs/plans/views-migration.md); their toolbar actions and popups
// are the Views window's (pane/ui/extensions).

NS_ASSUME_NONNULL_BEGIN

// "Add <extension>?": Chrome asks before installing from the Web Store, or
// for new permissions. Answered once.
PANE_EXPORT
NS_SWIFT_NAME(ExtensionInstallRequest)
NS_SWIFT_UI_ACTOR
@interface PaneExtensionInstallRequest : NSObject

@property(readonly, copy) NSString* title;
// The extension's Web Store id, or empty when Chrome gave none.
@property(readonly, copy) NSString* extensionIdentifier;
@property(readonly, copy) NSString* extensionName;
@property(readonly, nullable) NSImage* icon;
@property(readonly, copy) NSString* permissionsHeading;
@property(readonly, copy) NSArray<NSString*>* permissions;
@property(readonly, copy) NSString* acceptTitle;
@property(readonly, copy) NSString* cancelTitle;
@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

- (void)accept;
- (void)cancel;

@end

// MARK: - Extension details

// Where an extension may read and change sites' data, as the Site access
// pop-up of chrome://extensions' details page sets it (developerPrivate's
// HostAccess).
typedef NS_ENUM(NSInteger, PaneExtensionSiteAccess) {
  // No pop-up: it asks for no sites, or for some sites only (Chrome lists
  // those instead), or the user can't change them.
  PaneExtensionSiteAccessNone,
  PaneExtensionSiteAccessOnClick,
  PaneExtensionSiteAccessOnSpecificSites,
  PaneExtensionSiteAccessOnAllSites,
} NS_SWIFT_NAME(ExtensionSiteAccess);

// What chrome://extensions' details page shows of an extension, as of when it
// was asked for.
PANE_EXPORT
NS_SWIFT_NAME(ExtensionDetails)
NS_SWIFT_UI_ACTOR
@interface PaneExtensionDetails : NSObject

// Where it came from, as the details page words it: "Chrome Web Store",
// "Unpacked extension", "Added by policy"...
@property(readonly, copy) NSString* source;
// Its permissions, in Chrome's words. Its sites are siteAccess's where it
// has the pop-up, and listed here otherwise.
@property(readonly, copy) NSArray<NSString*>* permissions;
@property(readonly) PaneExtensionSiteAccess siteAccess;
// While siteAccess is On specific sites, the sites it was granted, as the
// details page lists them: Chrome's patterns ("*://example.com/*"), sorted.
@property(readonly, copy) NSArray<NSString*>* specificSites;
// Whether it wants file access, which shows "Allow access to file URLs".
@property(readonly) BOOL wantsFileAccess;
@property(readonly) BOOL allowsFileAccess;
// Its manifest's homepage_url.
@property(readonly, copy, nullable) NSURL* homepageURL;
// Its Web Store page, when it updates from the store.
@property(readonly, copy, nullable) NSURL* webStoreURL;

- (instancetype)init NS_UNAVAILABLE;

@end

// MARK: - Extension shortcuts

// One of an extension's commands (chrome.commands), as
// chrome://extensions/shortcuts lists it, as of when it was asked for.
PANE_EXPORT
NS_SWIFT_NAME(ExtensionCommand)
NS_SWIFT_UI_ACTOR
@interface PaneExtensionCommand : NSObject

// The manifest's name: "_execute_action" for the action's.
@property(readonly, copy) NSString* name;
// Its description; Chrome's "Activate the extension" for the action's.
@property(readonly, copy) NSString* summary;
// Its shortcut as Chrome writes it (Command::AcceleratorToString:
// "Command+Shift+Y", "Ctrl+Alt+Comma"), or empty when it has none.
@property(readonly, copy) NSString* shortcut;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEEXTENSIONS_H_
