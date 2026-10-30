// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEEXTENSIONS_H_
#define PANE_PUBLIC_PANEEXTENSIONS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

// Chrome extensions, per profile (PaneBrowserContext): Chrome's extension
// system runs them; the app shows their toolbar actions, popups, install
// prompts, and management.

NS_ASSUME_NONNULL_BEGIN

@class PaneExtensionPopup;

// One installed extension, as of the last report. An action's title, badge,
// and icon are the ones for the profile's active tab.
PANE_EXPORT
NS_SWIFT_NAME(Extension)
NS_SWIFT_UI_ACTOR
@interface PaneExtension : NSObject

@property(readonly, copy) NSString* identifier;
@property(readonly, copy) NSString* name;
@property(readonly, copy) NSString* version;
@property(readonly, copy) NSString* summary;
@property(readonly, getter=isEnabled) BOOL enabled;
// Shown in the toolbar (Chrome's pinned extensions).
@property(readonly, getter=isPinned) BOOL pinned;
@property(readonly, nullable) NSImage* icon;
@property(readonly, copy, nullable) NSURL* optionsURL;

// The toolbar action (chrome.action / browserAction / pageAction).
@property(readonly) BOOL hasAction;
@property(readonly, copy) NSString* actionTitle;
@property(readonly, copy) NSString* badgeText;
@property(readonly, nullable) NSColor* badgeBackgroundColor;
@property(readonly, nullable) NSColor* badgeTextColor;
@property(readonly, getter=isActionEnabled) BOOL actionEnabled;
@property(readonly) BOOL hasPopup;

- (instancetype)init NS_UNAVAILABLE;

@end

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

NS_SWIFT_NAME(ExtensionPopupDelegate)
NS_SWIFT_UI_ACTOR
@protocol PaneExtensionPopupDelegate <NSObject>
@optional
// The popup's page asked for a new size (it sizes to its content).
- (void)extensionPopup:(PaneExtensionPopup*)popup didResize:(NSSize)size;
// The popup closed itself (window.close()), or its extension went away.
- (void)extensionPopupDidClose:(PaneExtensionPopup*)popup;
@end

// An extension action's popup page. The app shows `nativeView` (in a
// popover, sized to `preferredSize`) and calls -close when it goes.
PANE_EXPORT
NS_SWIFT_NAME(ExtensionPopup)
NS_SWIFT_UI_ACTOR
@interface PaneExtensionPopup : NSObject

@property(readonly, copy) NSString* extensionIdentifier;
@property(readonly) NSView* nativeView;
@property(readonly) NSSize preferredSize;
@property(readonly, getter=isOpen) BOOL open;
@property(nullable, weak) id<PaneExtensionPopupDelegate> delegate;

- (instancetype)init NS_UNAVAILABLE;

- (void)close;

@end

// MARK: - Extension menu items

// What an extension's menu item draws as.
typedef NS_ENUM(NSInteger, PaneExtensionMenuItemKind) {
  PaneExtensionMenuItemKindNormal,
  PaneExtensionMenuItemKindCheckbox,
  PaneExtensionMenuItemKindRadio,
  PaneExtensionMenuItemKindSeparator,
  PaneExtensionMenuItemKindSubmenu,
} NS_SWIFT_NAME(ExtensionMenuItemKind);

// An item an extension adds to a context menu (chrome.contextMenus), as
// Chrome's own menu would show it: an extension with several items gets one
// submenu, under its name and icon. Choosing one goes back to the menu that
// made it.
PANE_EXPORT
NS_SWIFT_NAME(ExtensionMenuItem)
NS_SWIFT_UI_ACTOR
@interface PaneExtensionMenuItem : NSObject

@property(readonly) PaneExtensionMenuItemKind kind;
@property(readonly, copy) NSString* title;
@property(readonly, getter=isEnabled) BOOL enabled;
@property(readonly, getter=isChecked) BOOL checked;
@property(readonly, nullable) NSImage* icon;
// A submenu's items.
@property(readonly, copy) NSArray<PaneExtensionMenuItem*>* children;

- (instancetype)init NS_UNAVAILABLE;

@end

// What the engine gives an extension action's right-click menu, as for the
// window's active tab: the extension's own items (chrome.contextMenus in the
// action's context) and its site access on the tab's page.
PANE_EXPORT
NS_SWIFT_NAME(ExtensionActionMenu)
NS_SWIFT_UI_ACTOR
@interface PaneExtensionActionMenu : NSObject

@property(readonly, copy) NSArray<PaneExtensionMenuItem*>* items;
// As Chrome's action menu offers it (ExtensionContextMenuModel's page
// access items): This Can Read and Change Site Data, a submenu of When You
// Click the Extension, On <site>, and On All Sites, the current one checked,
// then Learn More About Site Access; or, where the extension can't run on
// the page, Can't Read or Change Site's Data, disabled. Empty when the
// extension asks for no site access (no host permissions, no activeTab).
@property(readonly, copy) NSArray<PaneExtensionMenuItem*>* siteAccessItems;

// Runs one of `items` or `siteAccessItems`.
- (void)runItem:(PaneExtensionMenuItem*)item NS_SWIFT_NAME(run(_:));

- (instancetype)init NS_UNAVAILABLE;

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

// What chrome://extensions' details page shows of an extension beyond its
// snapshot (PaneExtension), as of when it was asked for.
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
