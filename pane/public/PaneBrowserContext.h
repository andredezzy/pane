// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEBROWSERCONTEXT_H_
#define PANE_PUBLIC_PANEBROWSERCONTEXT_H_

#import <Foundation/Foundation.h>

#import "PaneExport.h"

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

@end

// One Pane profile: its own cookies, storage, cache, and extensions, backed by
// a Chromium profile directory. Tabs of it open in a PaneBrowserWindow. Its
// extensions are the Views window's (pane/ui/extensions) and Chrome's own
// pages' (chrome://extensions, in the profile's tab).
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

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERCONTEXT_H_
