// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEWEBCONTENTS_H_
#define PANE_PUBLIC_PANEWEBCONTENTS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

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

// `isLoading` holds while a new document loads in the main frame, as Chrome's
// own loading UI shows (an iframe's load is not the page's); `progress` is
// the main frame's, in [0, 1].
- (void)webContents:(PaneWebContents*)webContents
    didChangeLoading:(BOOL)isLoading
            progress:(double)progress;

- (void)webContents:(PaneWebContents*)webContents
    didUpdateFavicon:(NSImage*)favicon;

// canGoBack / canGoForward may have changed.
- (void)webContentsDidChangeNavigationState:(PaneWebContents*)webContents;

// The page's renderer is gone; the page shows nothing until reloaded.
- (void)webContents:(PaneWebContents*)webContents
    rendererDidTerminate:(PaneRendererTermination)termination;

// Video picture-in-picture started or ended. The floating player is
// Chromium's own window.
- (void)webContents:(PaneWebContents*)webContents
    didChangePictureInPicture:(BOOL)active;

@end

// A tab: a page in one of a window's profiles. Tabs live in Chrome's tab
// model (pages, popups and extensions see real tabs); the app shows them
// through `nativeView`. An extension may discard a tab (chrome.tabs.discard),
// which Chrome does by putting a new, empty page in its place: this object
// stays the tab's, now for that page, which loads again once shown. Its
// `nativeView` changes then; a view the app shows keeps its place, the new
// one put where the old one was. Chrome's own discarding (memory pressure)
// is off: the app's sleep does that job.
PANE_EXPORT
NS_SWIFT_NAME(WebContents)
NS_SWIFT_UI_ACTOR
@interface PaneWebContents : NSObject

@property(nullable, weak) id<PaneWebContentsDelegate> delegate;

// The page's view. Add it to a window; it sizes with its superview through
// autoresizing.
@property(readonly) NSView* nativeView;

// Whether the app shows this page: the selected tab of a window. A page that
// is not visible is a background tab, which Chromium throttles. While a page
// is visible, the engine still notices its window being covered or the screen
// locking, as Chrome does for its active tab. A discarded page loads again
// as it becomes visible.
@property(getter=isVisible) BOOL visible;

@property(readonly, copy) NSString* title;
@property(readonly, copy, nullable) NSURL* URL;
@property(readonly, getter=isLoading) BOOL loading;
// The page is playing sound now (Chrome's audio indicator).
@property(readonly, getter=isAudible) BOOL audible;
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

- (void)zoomIn;
- (void)zoomOut;
- (void)resetZoom;

// Gives the page keyboard focus.
- (void)focus;

// Leaves the page's fullscreen (the app's Esc).
- (void)exitFullscreen;

// Prints with the system print panel.
- (void)print NS_SWIFT_NAME(printPage());

// Closes the tab, running the page's beforeunload first: the page may ask to
// stay (a JavaScript dialog). The delegate hears didRemoveTab when it goes.
// Called from a delegate callback, it closes on the next turn
// (PaneBrowserApplication.h).
- (void)close;

// Closes the tab at once, without beforeunload: for unloading a profile or a
// tab the app keeps, whose page should simply go. Called from a delegate
// callback, on the next turn.
- (void)closeImmediately;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEWEBCONTENTS_H_
