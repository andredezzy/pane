// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROFILE_CACHE_H_
#define PANE_BROWSER_PROFILE_CACHE_H_

#include <cstdint>

#include "base/functional/callback_forward.h"

class Profile;

namespace pane {

// A profile's cache, as Trim measures and drops it (CONTEXT.md: Trim, ADR
// 0041).

// The HTTP cache's bytes, as Chrome computes them (the Electron app's
// getCacheSize): 0 when they can't be read, the profile or its network
// service gone. `done` always runs.
void ComputeCacheSize(Profile* profile,
                      base::OnceCallback<void(int64_t bytes)> done);

// Drops the HTTP cache, Cache Storage, and service workers, through Chrome's
// BrowsingDataRemover, as the Electron app's clearData(cache,
// serviceWorkers) did; cookies, local storage, and IndexedDB stay, so the
// profile stays signed in everywhere. `done` always runs.
void TrimCache(Profile* profile, base::OnceClosure done);

}  // namespace pane

#endif  // PANE_BROWSER_PROFILE_CACHE_H_
