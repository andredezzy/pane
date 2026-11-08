// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEBROWSERAPPLICATION_H_
#define PANE_PUBLIC_PANEBROWSERAPPLICATION_H_

#import <Foundation/Foundation.h>

#import "PaneExport.h"

@class PaneBrowserApplication;
@class PaneBrowserContext;

NS_ASSUME_NONNULL_BEGIN

// Posted on the main thread when the user re-activates Pane (for example by
// clicking its Dock icon).
PANE_EXPORT extern NSNotificationName const
    PaneBrowserApplicationShouldReopenNotification NS_SWIFT_NAME(BrowserApplication.shouldReopenNotification);

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

// Another app handed Pane `urls` to open: a link clicked in Mail or Slack
// with Pane as the default browser, a file opened with Pane, `open -a Pane
// <url>`. Any scheme the bundle registers (http, https, file), in the order
// given. URLs that arrive while the engine starts (the app launched to open
// them) come once it has started, after -browserApplicationDidStart:.
- (void)browserApplication:(PaneBrowserApplication*)application
                  openURLs:(NSArray<NSURL*>*)urls
    NS_SWIFT_NAME(browserApplication(_:open:));

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
// `completion` on the main thread with the context, or nil and an error: for
// an identifier that cannot be one folder name (empty, ".", "..", a "/" or
// ":", longer than 255 bytes), a profile whose folder cannot be made or that
// does not load, or once the engine started shutting down. `engineKey`, 32
// bytes, encrypts every file the profile writes (docs/adr/0121): it stays in
// Pane's processes' memory, until -dropEngineKeysWithCompletion:.
- (void)openContextWithIdentifier:(NSString*)identifier
                        engineKey:(NSData*)engineKey
                       completion:
                           (void (^)(PaneBrowserContext* _Nullable context,
                                     NSError* _Nullable error))completion
    NS_SWIFT_NAME(openContext(identifier:engineKey:completion:));

// Encrypts a closed profile's files written by an earlier Pane, in place, off
// the main thread, before its context opens (docs/adr/0121). `folder` is its
// folder in the volume, its Profile and Cache inside. A safety copy goes there
// first, as SafetyCopy, once. A kill at any point loses nothing: the next call
// goes on. `progress` gets the share done, on any thread; `completion`, on the
// main thread, whether it finished.
- (void)migrateContextWithIdentifier:(NSString*)identifier
                           engineKey:(NSData*)engineKey
                              folder:(NSURL*)folder
                            progress:(void (^)(double share))progress
                          completion:(void (^)(BOOL migrated))completion
    NS_SWIFT_NAME(migrateContext(identifier:engineKey:folder:progress:completion:));

// Puts a profile back as its safety copy has it, for a migration that left it
// failing to open. Runs off the main thread.
- (void)restoreContextWithIdentifier:(NSString*)identifier
                           engineKey:(NSData*)engineKey
                              folder:(NSURL*)folder
                          completion:(void (^)(BOOL restored))completion
    NS_SWIFT_NAME(restoreContext(identifier:engineKey:folder:completion:));

// Puts right what a migration before 0.18.2 did to a profile, once. Runs off
// the main thread, on a closed profile.
- (void)repairContextWithIdentifier:(NSString*)identifier
                          engineKey:(NSData*)engineKey
                             folder:(NSURL*)folder
                         completion:(void (^)(BOOL repaired))completion
    NS_SWIFT_NAME(repairContext(identifier:engineKey:folder:completion:));

// Forgets every profile's key in every process, once the lock's detach has left
// no profile's file open. A path of a profile's with no key then stops the
// process that reaches it. `completion` gets how many each process still holds,
// by "browser", "network", and "storage", each 0 when all went well.
- (void)dropEngineKeysWithCompletion:
    (void (^)(NSDictionary<NSString*, NSNumber*>* left))completion
    NS_SWIFT_NAME(dropEngineKeys(completion:));

// Deletes the profile: closes its tabs in every window, without asking their
// pages (no beforeunload), then removes its data (cookies, storage, cache,
// extensions) from disk. `completion` gets NO when the profile is unknown or
// already being deleted.
- (void)deleteContext:(PaneBrowserContext*)context
           completion:(void (^)(BOOL deleted))completion
    NS_SWIFT_NAME(deleteContext(_:completion:));

// Closes the profile and keeps its data: closes its tabs in every window,
// without asking their pages, and lets Chrome destroy the profile, which
// closes its files (Pane's lock, ADR 0116). `completion` gets NO when the
// profile is unknown, or still held after ten seconds.
- (void)closeContext:(PaneBrowserContext*)context
          completion:(void (^)(BOOL closed))completion
    NS_SWIFT_NAME(closeContext(_:completion:));

// Starts an orderly engine shutdown, as Quit does.
- (void)terminate;

// MARK: Fingerprints

// What a profile without a fingerprint presents: the engine's User-Agent, in
// Chrome's reduced form ("… Chrome/<major>.0.0.0 …"), and its full version
// ("<major>.0.<build>.<patch>", the engine's CHROMIUM_VERSION).
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

// Whether the profile named keeps its HTTP and code caches off the disk. Set it
// before the profile opens: a context reads it once, as it is created.
- (void)setKeepsCachesInMemory:(BOOL)inMemory
          forContextIdentifier:(NSString*)identifier
    NS_SWIFT_NAME(setKeepsCachesInMemory(_:forContextIdentifier:));

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERAPPLICATION_H_
