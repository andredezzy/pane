// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "components/proxy_config/proxy_prefs.h"
#import "pane/browser/browser_context_internal.h"
#include "pane/browser/downloads.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profiles.h"
#import "pane/public/PaneBrowserContext.h"

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
    pane::ApplyProfileDefaults(profile);
    [self applyFingerprint];
  }
  return self;
}

- (Profile*)profile {
  return _profile;
}

- (void)applyFingerprint {
  if (_profile) {
    pane::ApplyFingerprintLanguages(_profile);
  }
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
  PANE_TRACE("call", "PaneBrowserContext.setDownloadDirectory",
             base::DictValue().Set(
                 "path", base::SysNSStringToUTF8(downloadDirectory.path)));
  if (_profile) {
    DownloadPrefs::FromBrowserContext(_profile)->SetDownloadPath(
        base::FilePath(base::SysNSStringToUTF8(downloadDirectory.path)));
  }
}

// MARK: Network

- (NSString*)proxyRules {
  if (!_profile) {
    return nil;
  }
  ProxyConfigDictionary config(
      _profile->GetPrefs()->GetDict(proxy_config::prefs::kProxy).Clone());
  std::string server;
  ProxyPrefs::ProxyMode mode;
  if (!config.GetMode(&mode) || mode != ProxyPrefs::MODE_FIXED_SERVERS ||
      !config.GetProxyServer(&server)) {
    return nil;
  }
  return base::SysUTF8ToNSString(server);
}

- (void)setProxyRules:(NSString*)proxyRules {
  PANE_TRACE("call", "PaneBrowserContext.setProxyRules",
             base::DictValue().Set(
                 "rules", proxyRules ? base::SysNSStringToUTF8(proxyRules)
                                     : std::string()));
  if (!_profile) {
    return;
  }
  PrefService* prefs = _profile->GetPrefs();
  if (proxyRules.length) {
    prefs->SetDict(proxy_config::prefs::kProxy,
                   ProxyConfigDictionary::CreateFixedServers(
                       base::SysNSStringToUTF8(proxyRules), std::string()));
  } else {
    prefs->ClearPref(proxy_config::prefs::kProxy);
  }
}

- (NSString*)webRTCIPHandlingPolicy {
  return _profile ? base::SysUTF8ToNSString(_profile->GetPrefs()->GetString(
                        prefs::kWebRTCIPHandlingPolicy))
                  : @"default";
}

- (void)setWebRTCIPHandlingPolicy:(NSString*)policy {
  PANE_TRACE("call", "PaneBrowserContext.setWebRTCIPHandlingPolicy",
             base::DictValue().Set("policy", base::SysNSStringToUTF8(policy)));
  if (_profile) {
    _profile->GetPrefs()->SetString(prefs::kWebRTCIPHandlingPolicy,
                                    base::SysNSStringToUTF8(policy));
  }
}

- (void)shutDown {
  _downloads.reset();
  _keepAlive.reset();
  _profile = nullptr;
}

@end
