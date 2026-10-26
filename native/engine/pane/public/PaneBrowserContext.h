// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEBROWSERCONTEXT_H_
#define PANE_PUBLIC_PANEBROWSERCONTEXT_H_

#import <Foundation/Foundation.h>

#import "PaneExport.h"

@class PaneWebContents;

NS_ASSUME_NONNULL_BEGIN

// One Pane profile: its own cookies, storage, cache, and extensions, backed by
// a Chromium profile directory.
PANE_EXPORT
NS_SWIFT_NAME(BrowserContext)
NS_SWIFT_UI_ACTOR
@interface PaneBrowserContext : NSObject

@property(readonly, copy) NSString* identifier;

- (instancetype)init NS_UNAVAILABLE;

// Creates a page in this profile. It shows once its `nativeView` is in a
// visible window.
- (PaneWebContents*)createWebContents;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEBROWSERCONTEXT_H_
