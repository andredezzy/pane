// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "base/functional/bind.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#import "pane/browser/browser_context_internal.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profile_cache.h"
#include "pane/browser/site_data.h"
#import "pane/public/PaneCookies.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace {

PaneCookieSameSite FromNet(net::CookieSameSite same_site) {
  switch (same_site) {
    case net::CookieSameSite::NO_RESTRICTION:
      return PaneCookieSameSiteNone;
    case net::CookieSameSite::LAX_MODE:
      return PaneCookieSameSiteLax;
    case net::CookieSameSite::STRICT_MODE:
      return PaneCookieSameSiteStrict;
    case net::CookieSameSite::UNSPECIFIED:
      return PaneCookieSameSiteUnspecified;
  }
}

network::mojom::CookieManager* CookieManagerOf(Profile* profile) {
  return profile ? profile->GetDefaultStoragePartition()
                       ->GetCookieManagerForBrowserProcess()
                 : nullptr;
}

}  // namespace

@interface PaneCookie ()
- (instancetype)initWithCanonicalCookie:(const net::CanonicalCookie&)cookie;
@end

@implementation PaneCookie {
  NSString* _name;
  NSString* _value;
  NSString* _domain;
  NSString* _path;
  BOOL _secure;
  BOOL _httpOnly;
  NSDate* _expirationDate;
  PaneCookieSameSite _sameSite;
}

- (instancetype)initWithCanonicalCookie:(const net::CanonicalCookie&)cookie {
  if ((self = [super init])) {
    _name = base::SysUTF8ToNSString(cookie.Name());
    _value = base::SysUTF8ToNSString(cookie.Value());
    _domain = base::SysUTF8ToNSString(cookie.Domain());
    _path = base::SysUTF8ToNSString(cookie.Path());
    _secure = cookie.SecureAttribute();
    _httpOnly = cookie.IsHttpOnly();
    _expirationDate =
        cookie.IsPersistent()
            ? [NSDate
                  dateWithTimeIntervalSince1970:cookie.ExpiryDate()
                                                    .InSecondsFSinceUnixEpoch()]
            : nil;
    _sameSite = FromNet(cookie.SameSite());
  }
  return self;
}

- (BOOL)isHTTPOnly {
  return _httpOnly;
}

@end

@implementation PaneBrowserContext (PaneCookies)

- (void)getCookiesWithCompletion:(void (^)(NSArray<PaneCookie*>*))completion {
  PANE_TRACE("call", "PaneBrowserContext.getCookies");
  network::mojom::CookieManager* manager = CookieManagerOf(self.profile);
  if (!manager) {
    completion(@[]);
    return;
  }

  // Mojo drops a reply whose pipe closes (the network service restarting,
  // the profile going): the completion runs then too, with nothing.
  manager->GetAllCookies(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(^(const net::CookieList& cookies) {
        NSMutableArray<PaneCookie*>* result = [NSMutableArray array];
        for (const net::CanonicalCookie& cookie : cookies) {
          [result
              addObject:[[PaneCookie alloc] initWithCanonicalCookie:cookie]];
        }
        completion(result);
      }),
      net::CookieList()));
}

- (void)getCacheSizeWithCompletion:(void (^)(int64_t))completion {
  pane::ComputeCacheSize(self.profile, base::BindOnce(^(int64_t bytes) {
                           completion(bytes);
                         }));
}

- (void)trimCacheWithCompletion:(void (^)(void))completion {
  pane::TrimCache(self.profile, base::BindOnce(^{
                    completion();
                  }));
}

// MARK: History

- (BOOL)savesHistory {
  return !self.profile || !self.profile->GetPrefs()->GetBoolean(
                              prefs::kSavingBrowserHistoryDisabled);
}

- (void)setSavesHistory:(BOOL)savesHistory {
  PANE_TRACE(
      "call", "PaneBrowserContext.setSavesHistory",
      base::DictValue().Set("savesHistory", static_cast<bool>(savesHistory)));
  if (self.profile) {
    self.profile->GetPrefs()->SetBoolean(prefs::kSavingBrowserHistoryDisabled,
                                         !savesHistory);
  }
}

- (void)clearHistoryWithCompletion:(void (^)(void))completion {
  PANE_TRACE("call", "PaneBrowserContext.clearHistory");
  if (!self.profile) {
    completion();
    return;
  }

  // Site usage: the engagement scores, whose keys name each site, which the
  // history alone leaves. The cache: each entry names its whole URL.
  pane::RemoveSiteData(
      self.profile,
      chrome_browsing_data_remover::DATA_TYPE_HISTORY |
          chrome_browsing_data_remover::DATA_TYPE_SITE_USAGE_DATA |
          content::BrowsingDataRemover::DATA_TYPE_DOWNLOADS |
          content::BrowsingDataRemover::DATA_TYPE_CACHE,
      {}, base::BindOnce(^{
        completion();
      }));
}

@end
