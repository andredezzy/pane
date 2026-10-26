// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANECOOKIES_H_
#define PANE_PUBLIC_PANECOOKIES_H_

#import <Foundation/Foundation.h>

#import "PaneBrowserContext.h"
#import "PaneExport.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, PaneCookieSameSite) {
  PaneCookieSameSiteUnspecified,
  PaneCookieSameSiteNone,
  PaneCookieSameSiteLax,
  PaneCookieSameSiteStrict,
} NS_SWIFT_NAME(CookieSameSite);

// One cookie of a profile.
PANE_EXPORT
NS_SWIFT_NAME(Cookie)
@interface PaneCookie : NSObject

- (instancetype)initWithName:(NSString*)name
                       value:(NSString*)value
                      domain:(NSString*)domain
                        path:(NSString*)path
                      secure:(BOOL)secure
                    httpOnly:(BOOL)httpOnly
              expirationDate:(nullable NSDate*)expirationDate
                    sameSite:(PaneCookieSameSite)sameSite
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@property(readonly, copy) NSString* name;
@property(readonly, copy) NSString* value;
// A leading dot for a domain cookie (".google.com"), none for a host one.
@property(readonly, copy) NSString* domain;
@property(readonly, copy) NSString* path;
@property(readonly, getter=isSecure) BOOL secure;
@property(readonly, getter=isHTTPOnly) BOOL httpOnly;
// nil for a session cookie.
@property(readonly, copy, nullable) NSDate* expirationDate;
@property(readonly) PaneCookieSameSite sameSite;

@end

// A profile's cookies and site data. Completions run on the main thread.
@interface PaneBrowserContext (PaneCookies)

// Every cookie of the profile.
- (void)getCookiesWithCompletion:
    (void (^)(NSArray<PaneCookie*>* cookies))completion
    NS_SWIFT_NAME(cookies(completion:));

// Stores `cookie`, as a response from its domain would. NO when Chrome
// refuses it (a domain it cannot be set on, say).
- (void)setCookie:(PaneCookie*)cookie
       completion:(void (^)(BOOL stored))completion
    NS_SWIFT_NAME(setCookie(_:completion:));

// Deletes the cookie with `cookie`'s name, domain, and path.
- (void)deleteCookie:(PaneCookie*)cookie
          completion:(void (^)(BOOL deleted))completion
    NS_SWIFT_NAME(deleteCookie(_:completion:));

// Clears what `origins` stored besides cookies: their cache, local storage,
// IndexedDB, service workers, Cache Storage, background fetches, and file
// systems.
- (void)clearSiteDataForOrigins:(NSArray<NSURL*>*)origins
                     completion:(void (^)(void))completion
    NS_SWIFT_NAME(clearSiteData(origins:completion:));

// The bytes the profile's HTTP cache holds on disk.
- (void)getCacheSizeWithCompletion:(void (^)(int64_t bytes))completion
    NS_SWIFT_NAME(cacheSize(completion:));

// Drops the profile's cached bytes (CONTEXT.md: Trim): the HTTP cache, Cache
// Storage, and service workers, every site's. Cookies, local storage, and
// IndexedDB stay, so the profile stays signed in.
- (void)trimCacheWithCompletion:(void (^)(void))completion
    NS_SWIFT_NAME(trimCache(completion:));

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANECOOKIES_H_
