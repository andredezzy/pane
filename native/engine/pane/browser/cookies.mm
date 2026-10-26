// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneCookies.h"

#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_inclusion_status.h"
#include "net/cookies/cookie_options.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/site_data.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

#import "pane/browser/browser_context_internal.h"

namespace {

net::CookieSameSite ToNet(PaneCookieSameSite same_site) {
  switch (same_site) {
    case PaneCookieSameSiteNone:
      return net::CookieSameSite::NO_RESTRICTION;
    case PaneCookieSameSiteLax:
      return net::CookieSameSite::LAX_MODE;
    case PaneCookieSameSiteStrict:
      return net::CookieSameSite::STRICT_MODE;
    case PaneCookieSameSiteUnspecified:
      return net::CookieSameSite::UNSPECIFIED;
  }
}

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
// The URL a response that set the cookie came from.
@property(readonly) GURL sourceURL;
// Chrome's cookie: the stored one, or one built from the properties. Null
// when Chrome refuses the properties.
- (std::optional<net::CanonicalCookie>)canonicalCookie;
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
  std::optional<net::CanonicalCookie> _stored;
}

- (instancetype)initWithName:(NSString*)name
                       value:(NSString*)value
                      domain:(NSString*)domain
                        path:(NSString*)path
                      secure:(BOOL)secure
                    httpOnly:(BOOL)httpOnly
              expirationDate:(NSDate*)expirationDate
                    sameSite:(PaneCookieSameSite)sameSite {
  if ((self = [super init])) {
    _name = [name copy];
    _value = [value copy];
    _domain = [domain copy];
    _path = path.length ? [path copy] : @"/";
    _secure = secure;
    _httpOnly = httpOnly;
    _expirationDate = [expirationDate copy];
    _sameSite = sameSite;
  }
  return self;
}

- (instancetype)initWithCanonicalCookie:(const net::CanonicalCookie&)cookie {
  NSDate* expiration =
      cookie.IsPersistent()
          ? [NSDate dateWithTimeIntervalSince1970:cookie.ExpiryDate()
                                                      .InSecondsFSinceUnixEpoch()]
          : nil;
  if ((self = [self initWithName:base::SysUTF8ToNSString(cookie.Name())
                           value:base::SysUTF8ToNSString(cookie.Value())
                          domain:base::SysUTF8ToNSString(cookie.Domain())
                            path:base::SysUTF8ToNSString(cookie.Path())
                          secure:cookie.SecureAttribute()
                        httpOnly:cookie.IsHttpOnly()
                  expirationDate:expiration
                        sameSite:FromNet(cookie.SameSite())])) {
    _stored = cookie;
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

- (GURL)sourceURL {
  std::string host = base::SysNSStringToUTF8(_domain);
  if (host.starts_with(".")) {
    host = host.substr(1);
  }
  return GURL((_secure ? "https://" : "http://") + host +
              base::SysNSStringToUTF8(_path));
}

- (std::optional<net::CanonicalCookie>)canonicalCookie {
  if (_stored) {
    return _stored;
  }

  // A domain cookie names its domain with a leading dot; a host cookie has
  // none and belongs to the URL's host alone.
  const std::string domain = base::SysNSStringToUTF8(_domain);
  const base::Time now = base::Time::Now();
  net::CookieInclusionStatus status;
  std::unique_ptr<net::CanonicalCookie> cookie =
      net::CanonicalCookie::CreateSanitizedCookie(
          self.sourceURL, base::SysNSStringToUTF8(_name),
          base::SysNSStringToUTF8(_value),
          domain.starts_with(".") ? domain : std::string(),
          base::SysNSStringToUTF8(_path), now,
          _expirationDate ? base::Time::FromSecondsSinceUnixEpoch(
                                _expirationDate.timeIntervalSince1970)
                          : base::Time(),
          now, _secure, _httpOnly, ToNet(_sameSite),
          net::COOKIE_PRIORITY_DEFAULT, std::nullopt, &status);
  if (!cookie) {
    return std::nullopt;
  }
  return *cookie;
}

@end

@implementation PaneBrowserContext (PaneCookies)

- (void)getCookiesWithCompletion:(void (^)(NSArray<PaneCookie*>*))completion {
  pane::Trace("call", "PaneBrowserContext.getCookies");
  network::mojom::CookieManager* manager = CookieManagerOf(self.profile);
  if (!manager) {
    completion(@[]);
    return;
  }

  manager->GetAllCookies(base::BindOnce(^(const net::CookieList& cookies) {
    NSMutableArray<PaneCookie*>* result = [NSMutableArray array];
    for (const net::CanonicalCookie& cookie : cookies) {
      [result addObject:[[PaneCookie alloc] initWithCanonicalCookie:cookie]];
    }
    completion(result);
  }));
}

- (void)setCookie:(PaneCookie*)cookie completion:(void (^)(BOOL))completion {
  pane::Trace("call", "PaneBrowserContext.setCookie",
              base::DictValue()
                  .Set("name", base::SysNSStringToUTF8(cookie.name))
                  .Set("domain", base::SysNSStringToUTF8(cookie.domain)));
  network::mojom::CookieManager* manager = CookieManagerOf(self.profile);
  std::optional<net::CanonicalCookie> canonical = [cookie canonicalCookie];
  if (!manager || !canonical) {
    completion(NO);
    return;
  }

  manager->SetCanonicalCookie(
      *canonical, cookie.sourceURL, net::CookieOptions::MakeAllInclusive(),
      base::BindOnce(^(net::CookieAccessResult result) {
        completion(result.status.IsInclude());
      }));
}

- (void)deleteCookie:(PaneCookie*)cookie completion:(void (^)(BOOL))completion {
  pane::Trace("call", "PaneBrowserContext.deleteCookie",
              base::DictValue()
                  .Set("name", base::SysNSStringToUTF8(cookie.name))
                  .Set("domain", base::SysNSStringToUTF8(cookie.domain)));
  network::mojom::CookieManager* manager = CookieManagerOf(self.profile);
  std::optional<net::CanonicalCookie> canonical = [cookie canonicalCookie];
  if (!manager || !canonical) {
    completion(NO);
    return;
  }

  manager->DeleteCanonicalCookie(*canonical, base::BindOnce(^(bool deleted) {
                                   completion(deleted);
                                 }));
}

- (void)clearSiteDataForOrigins:(NSArray<NSURL*>*)origins
                     completion:(void (^)(void))completion {
  std::vector<url::Origin> filter;
  for (NSURL* origin in origins) {
    url::Origin parsed = url::Origin::Create(
        GURL(base::SysNSStringToUTF8(origin.absoluteString)));
    if (!parsed.opaque()) {
      filter.push_back(parsed);
    }
  }
  pane::Trace("call", "PaneBrowserContext.clearSiteData",
              base::DictValue().Set("origins", static_cast<int>(filter.size())));
  if (!self.profile || filter.empty()) {
    completion();
    return;
  }

  pane::RemoveSiteData(
      self.profile,
      content::BrowsingDataRemover::DATA_TYPE_CACHE |
          content::BrowsingDataRemover::DATA_TYPE_DOM_STORAGE,
      filter, base::BindOnce(^{
        completion();
      }));
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
          base::BindOnce(^(bool is_upper_bound, int64_t size_or_error) {
            completion(size_or_error < 0 ? 0 : size_or_error);
          }));
}

- (void)trimCacheWithCompletion:(void (^)(void))completion {
  pane::Trace("call", "PaneBrowserContext.trimCache");
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
