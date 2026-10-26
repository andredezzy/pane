// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEWEBCONTENTS_H_
#define PANE_PUBLIC_PANEWEBCONTENTS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

@class PaneBrowserContext;
@class PaneWebContents;

NS_ASSUME_NONNULL_BEGIN

NS_SWIFT_NAME(WebContentsDelegate)
NS_SWIFT_UI_ACTOR
@protocol PaneWebContentsDelegate <NSObject>
@optional

- (void)webContents:(PaneWebContents*)webContents
     didChangeTitle:(NSString*)title;
- (void)webContents:(PaneWebContents*)webContents didChangeURL:(NSURL*)URL;

// `isLoading` flips at the start and end of a load; `progress` is in [0, 1].
- (void)webContents:(PaneWebContents*)webContents
    didChangeLoading:(BOOL)isLoading
            progress:(double)progress;

- (void)webContents:(PaneWebContents*)webContents
    didUpdateFavicon:(NSImage*)favicon;

@end

// A page: Chromium's WebContents, shown through `nativeView`.
PANE_EXPORT
NS_SWIFT_NAME(WebContents)
NS_SWIFT_UI_ACTOR
@interface PaneWebContents : NSObject

@property(readonly) PaneBrowserContext* context;
@property(nullable, weak) id<PaneWebContentsDelegate> delegate;

// The page's view. Add it to a window; it sizes with its superview through
// autoresizing.
@property(readonly) NSView* nativeView;

// Whether the app shows this page: the selected tab of a window. A page that
// is not visible is a background tab, which Chromium throttles. While a page
// is visible, the engine still notices its window being covered or the screen
// locking, as Chrome does for its active tab.
@property(getter=isVisible) BOOL visible;

@property(readonly, copy) NSString* title;
@property(readonly, copy, nullable) NSURL* URL;
@property(readonly, getter=isLoading) BOOL loading;

- (instancetype)init NS_UNAVAILABLE;

- (void)loadURL:(NSURL*)URL NS_SWIFT_NAME(load(_:));

// Destroys the page. The object stays valid but inert afterwards.
- (void)close;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEWEBCONTENTS_H_
