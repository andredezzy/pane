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

// A profile's cookies, cache, and history. Completions run on the main
// thread.
@interface PaneBrowserContext (PaneCookies)

// Every cookie of the profile.
- (void)getCookiesWithCompletion:
    (void (^)(NSArray<PaneCookie*>* cookies))completion
    NS_SWIFT_NAME(cookies(completion:));

// The bytes the profile's HTTP cache holds on disk.
- (void)getCacheSizeWithCompletion:(void (^)(int64_t bytes))completion
    NS_SWIFT_NAME(cacheSize(completion:));

// Drops the profile's cached bytes (CONTEXT.md: Trim): the HTTP cache, Cache
// Storage, and service workers, every site's. Cookies, local storage, and
// IndexedDB stay, so the profile stays signed in.
- (void)trimCacheWithCompletion:(void (^)(void))completion
    NS_SWIFT_NAME(trimCache(completion:));

// Whether the profile keeps a record of where it went: the pages it visits
// (Chrome's history, and the visited-link colours drawn from it), their
// icons, its downloads, and what Chrome learns from its visits (the sites'
// engagement scores and the loading predictor). NO is Chrome's
// SavingBrowserHistoryDisabled policy, with those kept off too; the tabs,
// their back and forward lists, cookies, and site data are unaffected. YES
// until set, and kept in the profile's prefs.
@property BOOL savesHistory;

// Deletes what the profile kept of where it went, for all time, as Chrome's
// Clear browsing data does with Browsing history, Download history, and site
// usage data: an open tab keeps its page and loses its back and forward list.
// Cookies and site data stay, so the profile stays signed in.
- (void)clearHistoryWithCompletion:(void (^)(void))completion
    NS_SWIFT_NAME(clearHistory(completion:));

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANECOOKIES_H_
