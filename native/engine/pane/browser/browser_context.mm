// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserContext.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "pane/browser/engine_trace.h"

#import "pane/browser/browser_context_internal.h"
#import "pane/browser/web_contents_internal.h"

@implementation PaneBrowserContext {
  NSString* _identifier;
  raw_ptr<Profile> _profile;
  // A Pane context counts as an open window for its profile, so Chrome does
  // not unload the profile while the app shows it.
  std::unique_ptr<ScopedProfileKeepAlive> _keepAlive;
  NSHashTable<PaneWebContents*>* _webContents;
}

- (instancetype)initWithProfile:(Profile*)profile
                     identifier:(NSString*)identifier {
  if ((self = [super init])) {
    _identifier = [identifier copy];
    _profile = profile;
    _keepAlive = std::make_unique<ScopedProfileKeepAlive>(
        profile, ProfileKeepAliveOrigin::kBrowserWindow);
    _webContents = [NSHashTable weakObjectsHashTable];
  }
  return self;
}

- (NSString*)identifier {
  return _identifier;
}

- (Profile*)profile {
  return _profile;
}

- (PaneWebContents*)createWebContents {
  pane::Trace("call", "PaneBrowserContext.createWebContents",
              base::DictValue().Set("identifier",
                                    base::SysNSStringToUTF8(_identifier)));
  PaneWebContents* webContents =
      [[PaneWebContents alloc] initWithContext:self];
  [_webContents addObject:webContents];
  return webContents;
}

- (void)shutDown {
  for (PaneWebContents* webContents in _webContents.allObjects) {
    [webContents close];
  }
  [_webContents removeAllObjects];
  _keepAlive.reset();
  _profile = nullptr;
}

@end
