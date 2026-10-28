// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneCookies.h"


#include "base/functional/bind.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/site_data.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"

#import "pane/browser/browser_context_internal.h"

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
            ? [NSDate dateWithTimeIntervalSince1970:cookie.ExpiryDate()
                                                        .InSecondsFSinceUnixEpoch()]
            : nil;
    _sameSite = FromNet(cookie.SameSite());
  }
  return self;
}

- (NSString*)name {
  return _name;
}

- (NSString*)value {
  return _value;
}

- (NSString*)domain {
  return _domain;
}

- (NSString*)path {
  return _path;
}

- (BOOL)isSecure {
  return _secure;
}

- (BOOL)isHTTPOnly {
  return _httpOnly;
}

- (NSDate*)expirationDate {
  return _expirationDate;
}

- (PaneCookieSameSite)sameSite {
  return _sameSite;
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
  if (!self.profile) {
    completion(0);
    return;
  }

  self.profile->GetDefaultStoragePartition()
      ->GetNetworkContext()
      ->ComputeHttpCacheSize(
          base::Time(), base::Time::Max(),
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(^(bool is_upper_bound, int64_t size_or_error) {
                completion(size_or_error < 0 ? 0 : size_or_error);
              }),
              false, int64_t{0}));
}

- (void)trimCacheWithCompletion:(void (^)(void))completion {
  PANE_TRACE("call", "PaneBrowserContext.trimCache");
  if (!self.profile) {
    completion();
    return;
  }

  pane::RemoveSiteData(
      self.profile,
      content::BrowsingDataRemover::DATA_TYPE_CACHE |
          content::BrowsingDataRemover::DATA_TYPE_CACHE_STORAGE |
          content::BrowsingDataRemover::DATA_TYPE_SERVICE_WORKERS,
      {}, base::BindOnce(^{
        completion();
      }));
}

@end
