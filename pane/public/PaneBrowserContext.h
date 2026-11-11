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

// "Add <extension>?": Chrome asks before an install (the Web Store's "Add to
// Chrome", or -installExtensionWithIdentifier:) or for new permissions. Not
// implemented: Chrome's own dialog.
- (void)browserContext:(PaneBrowserContext*)context
    confirmExtensionInstall:(PaneExtensionInstallRequest*)request;

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
// A proxy asking for credentials asks in the window's sign-in dialog
// (pane/ui/dialogs/sign_in.h).
@property(nullable, copy) NSString* proxyRules;

// How WebRTC may use the network (Chrome's webrtc.ip_handling_policy):
// "default", "default_public_interface_only",
// "default_public_and_private_interfaces", or "disable_non_proxied_udp".
@property(copy) NSString* webRTCIPHandlingPolicy;

// MARK: Extensions

// Installs from the Chrome Web Store, asking first
// (browserContext:confirmExtensionInstall:).
- (void)installExtensionWithIdentifier:(NSString*)identifier
                                window:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(installExtension(identifier:window:));
- (void)setEnabled:(BOOL)enabled
      forExtension:(NSString*)identifier
    NS_SWIFT_NAME(setEnabled(_:forExtension:));
- (void)setPinned:(BOOL)pinned
     forExtension:(NSString*)identifier
    NS_SWIFT_NAME(setPinned(_:forExtension:));
- (void)uninstallExtension:(NSString*)identifier;
// Runs the extension shortcut (chrome.commands) `event`, a key down, is in
// `window`, as Chrome does: a command's onCommand, with the active tab. NO
// when it is none, when it is the action's (the Views window's ExtensionsUi
// runs actions, X1, and X5 hands it the shortcut), or when the menu bar has
// the key: the browser's shortcuts stay its own.
- (BOOL)runExtensionShortcut:(NSEvent*)event
                    inWindow:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(runExtensionShortcut(_:in:));
// The extension's commands, as chrome://extensions/shortcuts lists them: its
// action's, then its named ones, each with its shortcut. Empty when it is
// not installed or has none.
- (NSArray<PaneExtensionCommand*>*)commandsOfExtension:(NSString*)identifier
    NS_SWIFT_NAME(commands(ofExtension:));
// Sets the command's shortcut, as chrome://extensions/shortcuts does:
// `shortcut` as Chrome writes it (PaneExtensionCommand.shortcut,
// +extensionShortcutForEvent:), or empty to clear it. A key another command
// had moves here, as in Chrome: that command's extension is returned, or nil.
- (nullable NSString*)setShortcut:(NSString*)shortcut
                        ofCommand:(NSString*)command
                        extension:(NSString*)identifier
    NS_SWIFT_NAME(setShortcut(_:ofCommand:extension:));
// While one is recorded, no extension shortcut runs in `window`, as
// chrome://extensions/shortcuts suspends them.
- (void)setExtensionShortcutsSuspended:(BOOL)suspended
                              inWindow:(PaneBrowserWindow*)window
    NS_SWIFT_NAME(setExtensionShortcutsSuspended(_:in:));
// The shortcut `event`, a key down, types, as Chrome writes it
// ("Command+Shift+Y"), when Chrome takes it for a command; nil otherwise: no
// Ctrl, Alt, or Command, a combination Chrome refuses, or a key that can't
// end a shortcut.
+ (nullable NSString*)extensionShortcutForEvent:(NSEvent*)event
    NS_SWIFT_NAME(extensionShortcut(for:));
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
// The details page's Add a site: `site` as the user typed it
// ("example.com", "https://*.example.com"), granted with the extension On
// specific sites. NO, changing nothing, for one the page refuses ("Not a
// valid web address").
- (BOOL)addSite:(NSString*)site
    toExtension:(NSString*)identifier NS_SWIFT_NAME(addSite(_:toExtension:));
// Takes one of its specificSites away; with none left, it runs on click.
- (void)removeSite:(NSString*)site
     fromExtension:(NSString*)identifier
    NS_SWIFT_NAME(removeSite(_:fromExtension:));

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERCONTEXT_H_
