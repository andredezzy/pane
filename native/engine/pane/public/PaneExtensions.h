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

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEEXTENSIONS_H_
