// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEBROWSERAPPLICATION_H_
#define PANE_PUBLIC_PANEBROWSERAPPLICATION_H_

#import <Foundation/Foundation.h>

#import "PaneExport.h"

@class PaneBrowserApplication;
@class PaneBrowserContext;

NS_ASSUME_NONNULL_BEGIN

// Posted on the main thread when the user re-activates Pane (for example by
// clicking its Dock icon). `userInfo[PaneReopenHasVisibleWindowsKey]` is an
// NSNumber BOOL.
PANE_EXPORT extern NSNotificationName const
    PaneBrowserApplicationShouldReopenNotification
        NS_SWIFT_NAME(BrowserApplication.shouldReopenNotification);
PANE_EXPORT extern NSString* const PaneReopenHasVisibleWindowsKey
    NS_SWIFT_NAME(BrowserApplication.reopenHasVisibleWindowsKey);

NS_SWIFT_NAME(BrowserApplicationDelegate)
NS_SWIFT_UI_ACTOR
@protocol PaneBrowserApplicationDelegate <NSObject>

// The engine finished starting: profiles can be opened and web contents
// created. Called once, on the main thread.
- (void)browserApplicationDidStart:(PaneBrowserApplication*)application;

@optional
// The engine is shutting down: every window has closed and profiles are about
// to be released. The last moment to save the app's state.
- (void)browserApplicationWillShutDown:(PaneBrowserApplication*)application;

@end

// The engine hosted by the Pane app. The app's `main` calls
// `+runWithDelegate:dataDirectory:`, which runs Chromium on the main thread
// and returns only when the engine shuts down. Every class of this API is
// main-thread only, and every callback arrives on the main thread.
//
// Delegate callbacks run inside Chromium's own dispatch (a page's observer,
// a tab strip notification, a page's request). The app may read anything and
// navigate from them. Calls that destroy a tab or change a tab strip run on
// the next turn when made from a callback: PaneWebContents -close and
// -closeImmediately, PaneBrowserWindow -activateTab: and -close, and this
// class's -deleteContext:completion: and -terminate. -openTabWithURL:… does
// its work at once (it returns the tab), so it is not for tab strip
// callbacks (a window delegate's didInsertTab, didRemoveTab,
// didActivateTab).
PANE_EXPORT
NS_SWIFT_NAME(BrowserApplication)
NS_SWIFT_UI_ACTOR
@interface PaneBrowserApplication : NSObject

// The running application, or nil before the engine started.
@property(class, readonly, nullable) PaneBrowserApplication* sharedApplication;

@property(readonly, weak) id<PaneBrowserApplicationDelegate> delegate;

// Root of everything the engine stores; profiles live in its `Engine/`.
@property(readonly, copy) NSURL* dataDirectory;

// Runs the engine. Command-line switches the app was launched with (for
// example `--remote-debugging-port`) pass through to Chromium. Returns the
// process exit code.
+ (int)runWithDelegate:(id<PaneBrowserApplicationDelegate>)delegate
         dataDirectory:(NSURL*)dataDirectory
    NS_SWIFT_NAME(run(delegate:dataDirectory:));

- (instancetype)init NS_UNAVAILABLE;

// Opens, creating on first use, the profile named `identifier`. Calls
// `completion` on the main thread with the context, or nil and an error.
- (void)openContextWithIdentifier:(NSString*)identifier
                       completion:
                           (void (^)(PaneBrowserContext* _Nullable context,
                                     NSError* _Nullable error))completion
    NS_SWIFT_NAME(openContext(identifier:completion:));

// Deletes the profile: closes its tabs in every window, then removes its
// data (cookies, storage, cache, extensions) from disk. `completion` gets
// NO when the profile is unknown or already being deleted.
- (void)deleteContext:(PaneBrowserContext*)context
           completion:(void (^)(BOOL deleted))completion
    NS_SWIFT_NAME(deleteContext(_:completion:));

// Starts an orderly engine shutdown, as Quit does.
- (void)terminate;

// MARK: Fingerprints

// What a profile without a fingerprint presents: the engine's User-Agent
// ("… Chrome/156.0.0.0 …") and its full version ("156.0.8078.48").
@property(readonly, copy) NSString* userAgent;
@property(readonly, copy) NSString* chromiumVersion;

// What WebGL reports as its unmasked vendor and renderer on this Mac, for a
// profile without a fingerprint (Chrome's GPU info), or nil before the GPU
// process reported them.
@property(readonly, copy, nullable) NSString* hostWebGLVendor;
@property(readonly, copy, nullable) NSString* hostWebGLRenderer;

// Makes the profile named `identifier` present a fingerprint instead of
// Chrome's own values, or Chrome's own again for nil. `json` is PaneKit's
// EngineFingerprint: the User-Agent and its Client Hints, the languages, and
// what Blink reports in every frame and worker (navigator, screen, WebGL,
// canvas and audio noise, time zone, locale). Set it before the profile
// opens: its network context takes the User-Agent as it is created, and each
// renderer takes the rest as it starts. Navigations and new renderers follow
// a change at once. NO, with `error`, for JSON that is not a fingerprint.
- (BOOL)setFingerprint:(nullable NSString*)json
    forContextIdentifier:(NSString*)identifier
                   error:(NSError**)error
    NS_SWIFT_NAME(setFingerprint(_:forContextIdentifier:));

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERAPPLICATION_H_
