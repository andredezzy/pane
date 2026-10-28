// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEWEBCONTENTS_H_
#define PANE_PUBLIC_PANEWEBCONTENTS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

@class PaneAuthenticationChallenge;
@class PaneBrowserContext;
@class PaneContextMenu;
@class PaneExternalProtocolRequest;
@class PaneJavaScriptDialog;
@class PanePermissionRequest;
@class PaneUnresponsivePage;
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

// What Chrome would show in its own dialogs and bubbles (PaneDialogs.h).
// Each is answered through the request; one the delegate does not take is
// cancelled (dialogs, authentication, external protocols), dismissed
// (permissions), or waited on (an unresponsive page).
- (void)webContents:(PaneWebContents*)webContents
    runJavaScriptDialog:(PaneJavaScriptDialog*)dialog;
- (void)webContents:(PaneWebContents*)webContents
    requestPermission:(PanePermissionRequest*)request;
- (void)webContents:(PaneWebContents*)webContents
       authenticate:(PaneAuthenticationChallenge*)challenge;
- (void)webContents:(PaneWebContents*)webContents
    openExternalProtocol:(PaneExternalProtocolRequest*)request;
- (void)webContents:(PaneWebContents*)webContents
    becameUnresponsive:(PaneUnresponsivePage*)page;
// The engine withdrew a request the app had not answered: the page
// navigated, recovered, or closed. The app takes down its UI for it.
- (void)webContents:(PaneWebContents*)webContents
    didDismissRequest:(id)request;

// A right-click in the page. Not implemented: Chrome's own menu.
- (void)webContents:(PaneWebContents*)webContents
    showContextMenu:(PaneContextMenu*)menu;

// The page started printing (window.print() or -print); the system print
// panel follows.
- (void)webContentsWillPrint:(PaneWebContents*)webContents;

// Video picture-in-picture started or ended. The floating player is
// Chromium's own window.
- (void)webContents:(PaneWebContents*)webContents
    didChangePictureInPicture:(BOOL)active;

// DevTools docked to this tab appeared (`devTools` is its page, to show
// with this one at devToolsPageBounds over it), moved, or went away (nil).
- (void)webContents:(PaneWebContents*)webContents
    didChangeDevTools:(nullable PaneWebContents*)devTools;

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
// The page is playing sound now (Chrome's audio indicator).
@property(readonly, getter=isAudible) BOOL audible;
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

// Leaves the page's fullscreen (the app's Esc).
- (void)exitFullscreen;

// Prints with the system print panel.
- (void)print NS_SWIFT_NAME(printPage());

// DevTools for this tab, docked beside it (see the delegate).
@property(readonly, getter=isDevToolsOpen) BOOL devToolsOpen;
// While DevTools are docked, where this page sits over the DevTools page, in
// its coordinates (origin at the top left); empty while DevTools cover it.
// The delegate hears didChangeDevTools: whenever it moves.
@property(readonly) NSRect devToolsPageBounds;
- (void)showDevTools;
- (void)closeDevTools;

// Closes the tab, running the page's beforeunload first: the page may ask to
// stay (a JavaScript dialog). The delegate hears didRemoveTab when it goes.
// Called from a delegate callback, it closes on the next turn
// (PaneBrowserApplication.h).
- (void)close;

// Closes the tab at once, without beforeunload: for unloading a profile or a
// tab the app keeps, whose page should simply go. Called from a delegate
// callback, on the next turn. Requests the app holds for the page (a
// JavaScript dialog, an unresponsive page, an external protocol) are
// withdrawn as it goes (didDismissRequest:), as for any tab that closes.
- (void)closeImmediately;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEWEBCONTENTS_H_
