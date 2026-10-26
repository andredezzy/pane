// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserContext.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "pane/browser/downloads.h"
#include "pane/browser/engine_trace.h"

#import "pane/browser/browser_context_internal.h"

@implementation PaneBrowserContext {
  NSString* _identifier;
  raw_ptr<Profile> _profile;
  __weak id<PaneBrowserContextDelegate> _delegate;
  // A Pane context counts as an open window for its profile, so Chrome does
  // not unload the profile while the app shows it.
  std::unique_ptr<ScopedProfileKeepAlive> _keepAlive;
  std::unique_ptr<pane::DownloadsBridge> _downloads;
}

- (instancetype)initWithProfile:(Profile*)profile
                     identifier:(NSString*)identifier {
  if ((self = [super init])) {
    _identifier = [identifier copy];
    _profile = profile;
    _keepAlive = std::make_unique<ScopedProfileKeepAlive>(
        profile, ProfileKeepAliveOrigin::kBrowserWindow);
    _downloads = std::make_unique<pane::DownloadsBridge>(self, profile);
    // Downloads go straight to the download directory, never to a dialog.
    profile->GetPrefs()->SetBoolean(prefs::kPromptForDownload, false);
  }
  return self;
}

- (NSString*)identifier {
  return _identifier;
}

- (Profile*)profile {
  return _profile;
}

- (id<PaneBrowserContextDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<PaneBrowserContextDelegate>)delegate {
  _delegate = delegate;
}

- (NSURL*)downloadDirectory {
  if (!_profile) {
    return [NSURL fileURLWithPath:NSTemporaryDirectory()];
  }
  base::FilePath path =
      DownloadPrefs::FromBrowserContext(_profile)->DownloadPath();
  return [NSURL fileURLWithPath:base::SysUTF8ToNSString(path.value())
                    isDirectory:YES];
}

- (void)setDownloadDirectory:(NSURL*)downloadDirectory {
  pane::Trace("call", "PaneBrowserContext.setDownloadDirectory",
              base::DictValue().Set(
                  "path", base::SysNSStringToUTF8(downloadDirectory.path)));
  if (_profile) {
    DownloadPrefs::FromBrowserContext(_profile)->SetDownloadPath(
        base::FilePath(base::SysNSStringToUTF8(downloadDirectory.path)));
  }
}

- (void)shutDown {
  _downloads.reset();
  _keepAlive.reset();
  _profile = nullptr;
}

@end
