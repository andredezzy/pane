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

@end

// The engine hosted by the Pane app. The app's `main` calls
// `+runWithDelegate:dataDirectory:`, which runs Chromium on the main thread
// and returns only when the engine shuts down. Every class of this API is
// main-thread only, and every callback arrives on the main thread.
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

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERAPPLICATION_H_
