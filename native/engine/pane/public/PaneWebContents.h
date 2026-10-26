// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEWEBCONTENTS_H_
#define PANE_PUBLIC_PANEWEBCONTENTS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

@class PaneBrowserContext;
@class PaneWebContents;

NS_ASSUME_NONNULL_BEGIN

// Why a page's renderer went away.
typedef NS_ENUM(NSInteger, PaneRendererTermination) {
  PaneRendererTerminationCrashed,
  PaneRendererTerminationKilled,
  PaneRendererTerminationOutOfMemory,
  PaneRendererTerminationOther,
} NS_SWIFT_NAME(RendererTermination);

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

// canGoBack / canGoForward may have changed.
- (void)webContentsDidChangeNavigationState:(PaneWebContents*)webContents;

// Find in page results; `final` is the last update for the current search.
- (void)webContents:(PaneWebContents*)webContents
     didFindMatches:(NSInteger)count
        activeMatch:(NSInteger)activeMatch
              final:(BOOL)final;

// The page's renderer is gone; the page shows nothing until reloaded.
- (void)webContents:(PaneWebContents*)webContents
    rendererDidTerminate:(PaneRendererTermination)termination;

@end

// A tab: a page in one of a window's profiles. Tabs live in Chrome's tab
// model (pages, popups and extensions see real tabs); the app shows them
// through `nativeView`.
PANE_EXPORT
NS_SWIFT_NAME(WebContents)
NS_SWIFT_UI_ACTOR
@interface PaneWebContents : NSObject

// Nil once the tab closed.
@property(readonly, nullable) PaneBrowserContext* context;
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
@property(readonly) double loadProgress;
@property(readonly, nullable) NSImage* favicon;
@property(readonly) BOOL canGoBack;
@property(readonly) BOOL canGoForward;
// Whether the tab still exists in Chrome's tab model.
@property(readonly, getter=isOpen) BOOL open;

- (instancetype)init NS_UNAVAILABLE;

- (void)loadURL:(NSURL*)URL NS_SWIFT_NAME(load(_:));
- (void)goBack;
- (void)goForward;
- (void)reload;
- (void)stopLoading;

// Finds `text`, moving to the next (or previous) match on repeated calls
// with the same text. Results arrive through the delegate.
- (void)findText:(NSString*)text forward:(BOOL)forward;
- (void)stopFinding;

- (void)zoomIn;
- (void)zoomOut;
- (void)resetZoom;

// Gives the page keyboard focus.
- (void)focus;

// Closes the tab, running the page's beforeunload first.
- (void)close;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEWEBCONTENTS_H_
