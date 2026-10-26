// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_WEB_CONTENTS_INTERNAL_H_
#define PANE_BROWSER_WEB_CONTENTS_INTERNAL_H_

#import "pane/public/PaneWebContents.h"

namespace content {
class WebContents;
}

@interface PaneWebContents ()

// Made by the tab's TabBridge, which outlives no WebContents.
- (instancetype)initWithContents:(content::WebContents*)contents;

// Nil once the tab is gone.
@property(readonly) content::WebContents* contents;

// From the TabBridge.
- (void)contentsDestroyed;
- (void)bridgeDidChangeTitle;
- (void)bridgeDidChangeURL;
- (void)bridgeDidChangeNavigationState;
- (void)bridgeDidChangeLoading:(BOOL)isLoading progress:(double)progress;
- (void)bridgeDidUpdateFavicon:(NSImage*)favicon;
- (void)bridgeDidFindMatches:(NSInteger)count
                 activeMatch:(NSInteger)activeMatch
                       final:(BOOL)final;
- (void)bridgeRendererDidTerminate:(PaneRendererTermination)termination;
- (void)bridgeDidChangePictureInPicture:(BOOL)active;

@end

#endif  // PANE_BROWSER_WEB_CONTENTS_INTERNAL_H_
