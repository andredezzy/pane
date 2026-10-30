// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEBROWSERCONTEXT_H_
#define PANE_PUBLIC_PANEBROWSERCONTEXT_H_

#import <Foundation/Foundation.h>

#import "PaneExport.h"
#import "PaneExtensions.h"

@class NSEvent;
@class PaneBrowserContext;
@class PaneBrowserWindow;

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, PaneDownloadState) {
  PaneDownloadStateInProgress,
  PaneDownloadStateComplete,
  PaneDownloadStateCancelled,
  PaneDownloadStateInterrupted,
} NS_SWIFT_NAME(DownloadState);

// A snapshot of one download.
PANE_EXPORT
NS_SWIFT_NAME(Download)
@interface PaneDownload : NSObject

@property(readonly, copy) NSString* identifier;
@property(readonly, copy) NSURL* URL;
// Where the file is being written, once Chrome picked it.
@property(readonly, copy, nullable) NSURL* fileURL;
@property(readonly) int64_t receivedBytes;
// -1 while unknown.
@property(readonly) int64_t totalBytes;
@property(readonly) PaneDownloadState state;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_SWIFT_NAME(BrowserContextDelegate)
NS_SWIFT_UI_ACTOR
@protocol PaneBrowserContextDelegate <NSObject>
@optional

// A download started, progressed, or finished.
- (void)browserContext:(PaneBrowserContext*)context
     didUpdateDownload:(PaneDownload*)download;

// Extensions (PaneExtensions.h) changed: installed, removed, enabled,
// disabled, pinned, or an action's title, badge, or icon.
- (void)browserContextDidChangeExtensions:(PaneBrowserContext*)context;

// "Add <extension>?": Chrome asks before an install (the Web Store's "Add to
// Chrome", or -installExtensionWithIdentifier:) or for new permissions. Not
// implemented: Chrome's own dialog.
- (void)browserContext:(PaneBrowserContext*)context
    confirmExtensionInstall:(PaneExtensionInstallRequest*)request;

// An extension was installed, where Chrome would show its "added" bubble.
- (void)browserContext:(PaneBrowserContext*)context
    didInstallExtension:(PaneExtension*)extension;

// An action's popup is ready to show: the user ran the action, or the
// extension called chrome.action.openPopup().
- (void)browserContext:(PaneBrowserContext*)context
    showExtensionPopup:(PaneExtensionPopup*)popup
              inWindow:(PaneBrowserWindow*)window;

@end

// One Pane profile: its own cookies, storage, cache, and extensions, backed by
// a Chromium profile directory. Tabs of it open in a PaneBrowserWindow.
PANE_EXPORT
NS_SWIFT_NAME(BrowserContext)
NS_SWIFT_UI_ACTOR
@interface PaneBrowserContext : NSObject

@property(readonly, copy) NSString* identifier;
@property(nullable, weak) id<PaneBrowserContextDelegate> delegate;

// Where downloads go, without asking. Chrome's default is ~/Downloads.
@property(copy) NSURL* downloadDirectory;

// MARK: Network

// The profile's proxy, as Chrome's fixed proxy rules: "http://host:port",
// "https://…", "socks4://…", "socks5://…" (Chrome's "proxy" pref, fixed
// servers). nil uses the system's settings. Loopback is never proxied.
// A proxy asking for credentials reaches the tab's delegate
// (webContents:authenticate: with a proxy challenge).
@property(nullable, copy) NSString* proxyRules;

// How WebRTC may use the network (Chrome's webrtc.ip_handling_policy):
// "default", "default_public_interface_only",
// "default_public_and_private_interfaces", or "disable_non_proxied_udp".
@property(copy) NSString* webRTCIPHandlingPolicy;

// MARK: Extensions

// Every extension installed in this profile, actions as for its active tab.
@property(readonly) NSArray<PaneExtension*>* extensions;

// Installs from the Chrome Web Store, asking first
// (browserContext:confirmExtensionInstall:).
- (void)installExtensionWithIdentifier:(NSString*)identifier
                                window:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(installExtension(identifier:window:));
- (void)setEnabled:(BOOL)enabled forExtension:(NSString*)identifier
    NS_SWIFT_NAME(setEnabled(_:forExtension:));
- (void)setPinned:(BOOL)pinned forExtension:(NSString*)identifier
    NS_SWIFT_NAME(setPinned(_:forExtension:));
- (void)uninstallExtension:(NSString*)identifier;
// A click on the extension's action in `window`: opens its popup
// (browserContext:showExtensionPopup:inWindow:) or fires onClicked.
- (void)runActionOfExtension:(NSString*)identifier
                    inWindow:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(runAction(ofExtension:in:));
// Runs the extension shortcut (chrome.commands) `event`, a key down, is in
// `window`, as Chrome does: a command's onCommand, with the active tab, or
// the action, as a click on it. NO when it is none, or when the menu bar
// has the key: the browser's shortcuts stay its own.
- (BOOL)runExtensionShortcut:(NSEvent*)event
                    inWindow:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(runExtensionShortcut(_:in:));
// The extension's own items and site access for its action's right-click
// menu, as for the active tab of `window`; nil when it has neither.
- (nullable PaneExtensionActionMenu*)actionMenuOfExtension:(NSString*)identifier
                                                  inWindow:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(actionMenu(ofExtension:in:));
// The extension's details, as chrome://extensions shows them; nil when it is
// not installed.
- (nullable PaneExtensionDetails*)detailsOfExtension:(NSString*)identifier
    NS_SWIFT_NAME(details(ofExtension:));
// The size of the extension's files, as the details page words it ("41.3
// MB", "< 1 MB"), measured off the main thread. `completion` runs on the
// main thread, with nil when it is not installed.
- (void)sizeOfExtension:(NSString*)identifier
             completion:(void (^)(NSString* _Nullable size))completion
    NS_SWIFT_NAME(size(ofExtension:completion:));
// Where it may read and change sites' data, as the details page's pop-up
// sets it. None, or an extension without the pop-up, changes nothing.
- (void)setSiteAccess:(PaneExtensionSiteAccess)access
         forExtension:(NSString*)identifier
    NS_SWIFT_NAME(setSiteAccess(_:forExtension:));
// Allow access to file URLs. Chrome reloads the extension to apply it.
- (void)setAllowsFileAccess:(BOOL)allows
               forExtension:(NSString*)identifier
    NS_SWIFT_NAME(setAllowsFileAccess(_:forExtension:));

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERCONTEXT_H_
