// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEBROWSERWINDOW_H_
#define PANE_PUBLIC_PANEBROWSERWINDOW_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

@class PaneBrowserContext;
@class PaneBrowserWindow;
@class PaneWebContents;

NS_ASSUME_NONNULL_BEGIN

NS_SWIFT_NAME(BrowserWindowDelegate)
NS_SWIFT_UI_ACTOR
@protocol PaneBrowserWindowDelegate <NSObject>
@optional

// A tab was added to one of the window's profiles: by the app, or by a page
// (a popup, a link opened in a new tab or window). `opener` is the tab it
// came from, if any. `foreground` says whether it should become selected.
- (void)browserWindow:(PaneBrowserWindow*)window
         didInsertTab:(PaneWebContents*)tab
               opener:(nullable PaneWebContents*)opener
           foreground:(BOOL)foreground;

// A tab left the window: it closed, or the page closed itself.
- (void)browserWindow:(PaneBrowserWindow*)window
         didRemoveTab:(PaneWebContents*)tab;

// Chrome made another tab of a profile active (for example after the active
// one closed).
- (void)browserWindow:(PaneBrowserWindow*)window
       didActivateTab:(PaneWebContents*)tab;

// A page entered or left full screen (the Fullscreen API).
- (void)browserWindow:(PaneBrowserWindow*)window
                    tab:(PaneWebContents*)tab
    didChangeFullscreen:(BOOL)fullscreen;

// The user asked to close the window; NO keeps it open.
- (BOOL)browserWindowShouldClose:(PaneBrowserWindow*)window;
// Every page agreed to close; the window is about to go.
- (void)browserWindowWillClose:(PaneBrowserWindow*)window;

// A key press in a page, before the page sees it; YES consumes it.
- (BOOL)browserWindow:(PaneBrowserWindow*)window
    preHandleKeyEvent:(NSEvent*)event;
// A key press the page did not handle; YES consumes it.
- (BOOL)browserWindow:(PaneBrowserWindow*)window
       handleKeyEvent:(NSEvent*)event;

@end

// One window of the app. The engine makes the NSWindow (a Chrome Views
// window, so Chrome's own dialogs have one to attach to); the app draws all
// of its UI in `hostView`, which covers the window's content area. Each
// profile shown in the window has its own tabs.
PANE_EXPORT
NS_SWIFT_NAME(BrowserWindow)
NS_SWIFT_UI_ACTOR
@interface PaneBrowserWindow : NSObject

@property(readonly) NSWindow* window;
@property(readonly) NSView* hostView;
@property(nullable, weak) id<PaneBrowserWindowDelegate> delegate;

// The profile the app shows. Chrome treats its tabs as the window's.
@property(nullable) PaneBrowserContext* activeContext;

- (instancetype)initWithContentRect:(NSRect)rect;
- (instancetype)init NS_UNAVAILABLE;

// Opens a tab in `context` (about:blank without a URL). The delegate hears
// of it as an inserted tab first.
- (nullable PaneWebContents*)openTabWithURL:(nullable NSURL*)URL
                                  inContext:(PaneBrowserContext*)context
                                 foreground:(BOOL)foreground
    NS_SWIFT_NAME(openTab(url:context:foreground:));

// Makes `tab` its profile's active tab in Chrome's model.
- (void)activateTab:(PaneWebContents*)tab;

- (NSArray<PaneWebContents*>*)tabsInContext:(PaneBrowserContext*)context;

// Closes the window, running every page's beforeunload first.
- (void)close;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERWINDOW_H_
