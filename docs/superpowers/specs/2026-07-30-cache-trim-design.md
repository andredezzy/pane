# Cache Trim Design

## Goal

Stop a profile's cache from growing without bound. Partition isolation means no cache is ever shared between profiles, so every profile downloads and stores its own copy of the same web. Measured on a real install: 29 partitions holding **10.6 GB of cache** against **765 MB of actual profile data** — a 14:1 ratio. The worst single profile held 1.83 GB of cache and 26 MB of identity.

Pane already releases memory as a profile goes idle (Sleep, Unload). It never releases disk. This adds the disk rung.

## Scope

- A profile that unloads **and** is over its cache budget has its cache trimmed.
- One new setting, `cacheBudgetMB`, following the existing `number | null` convention.
- No new timer, no new subsystem: the trim hangs off the unload decision `SleepScheduler` already makes.
- **Out of scope:** manual trim UI, per-profile budgets, trimming on quit, and any change to `security/wipe.ts` (a different feature with a different promise).

## Language

A new `CONTEXT.md` entry, the third rung after Sleep and Unload:

> **Trim (cache)**:
> Dropping a profile's cached bytes — HTTP cache, service worker caches, compiled-script cache — while its identity survives untouched. Cookies, local storage, and IndexedDB are never cleared, so a trimmed profile is still signed in to everything. Happens automatically when an unloading profile is over its cache budget.
> _Avoid_: clear browsing data (Chrome's version takes cookies too, so it breaks the promise), wipe (means the security panic-wipe here), evict (Chromium jargon)

## Why not just cap the cache

Rejected after checking the platform. Electron 41.4.0 exposes **no per-session cache cap** — there is no `cacheSize` option on `session.fromPartition` and no `session.setCacheSize`.

The `--disk-cache-size` command-line switch exists but does not solve this:

- Chromium treats it as **a hint, not a limit**. Per Microsoft's Edge policy documentation for the same engine: "The value defined in this policy is treated as a suggestion to the caching system, not a strict limit... The total disk usage of all caches can be larger than (but within the same order of magnitude as) the configured value."
- It is applied per cache instance, so 29 partitions multiply it.
- It governs only the HTTP cache. In the measured profile that was 738 MB of 1.83 GB — the 784 MB `Service Worker` bucket is quota-managed storage and the 310 MB `Code Cache` is a separate V8 backend. Neither is reached.

Clearing on a trigger is the only lever that reaches all three buckets.

## Setting

`AppSettings` in `src/stores/settings-store.ts` gains one field:

```typescript
// Megabytes of cache a profile may hold before an unload trims it; null = off.
cacheBudgetMB: number | null;
```

Default `300`. The store's existing `merge` already folds new fields into defaults, so an old `settings.json` picks this up without migration.

`budget` is deliberate, not Chromium's `quota`: `quota` implies the browser enforces a ceiling. Nothing enforces this one — Pane compares against it at unload time and acts. Budget is the honest word for a threshold that is checked rather than imposed.

## Architecture

`SleepScheduler` is unit-testable today precisely because it never imports Electron — it talks to the narrow `SleepableTabs` interface and tests hand it a fake. That property is worth preserving, so the cache reaches it the same way.

```typescript
interface TrimmableCache {
  size(): Promise<number>;  // bytes
  trim(): Promise<void>;
}

interface ProfileHost {
  readonly profiles: ReadonlyMap<
    string,
    { readonly tabs: SleepableTabs; readonly cache: TrimmableCache }
  >;
}
```

`Profile` implements `TrimmableCache` against its real session (`session.getCacheSize()`, `session.clearData(...)`). `SleepScheduler` stays Electron-free.

**Trigger point.** Inside the existing unload branch in `tick()`, immediately after `profile.tabs.unloadAll()`. The unload branch has already established everything the trim needs: the profile is not active, not `keepLoaded`, has no protected tabs, and has been hidden past its timer. No second set of conditions.

**Never awaited.** `tick()` is synchronous and runs every 60s. The trim is fired and its rejection logged, never awaited — a slow disk must not stall the scheduler. An in-flight trim for a profile is tracked so a later tick cannot start a second one on top of it.

**Budget check.** `size()` is only called when `cacheBudgetMB !== null`, so the setting being off costs nothing.

## What gets cleared

```typescript
session.clearData({ dataTypes: ["cache", "serviceWorkers"] });
```

`cache` reaches the HTTP `Cache` directory; `serviceWorkers` reaches the `Service Worker` CacheStorage tree, which was the single largest bucket in the measurement.

Never included: `cookies`, `localStorage`, `indexedDB`. Those are the identity, and keeping them is the entire promise of the word Trim. A trimmed profile stays signed in.

## Open verification

**Does `dataTypes: ["cache"]` clear the `Code Cache` directory?** Electron's documentation does not say, and it is a distinct on-disk directory from `Cache` (310 MB in the measured profile). This is not settled by reading docs.

Implementation resolves it empirically: measure the profile's `Code Cache` directory before and after a real trim. If it survives, add `session.clearStorageData({ storages: ["shadercache", "cachestorage"] })` alongside. The result is recorded in the implementation plan — it is not assumed either way here.

## Tests

`src/main/sleep-scheduler.test.ts` extends its existing fake host with a fake cache. New cases:

- Over budget on unload → `trim()` called once.
- Under budget on unload → `trim()` never called.
- `cacheBudgetMB` is `null` → neither `size()` nor `trim()` is called.
- `keepLoaded` profile → never trimmed, because it never unloads.
- Tab-sleep path (no unload) → never trimmed.
- A `trim()` still in flight → a second tick does not start another.
- A rejected `trim()` → logged, and the scheduler keeps ticking.

## Settings UI

The sleep and unload settings are preset button rows, not free-form fields — `SleepTimerRow` in `src/renderer/app/settings/_components/memory-settings.tsx`, driven by an options array where `Off` carries `null`. The cache budget is the same control with the same reasoning (a byte-exact budget would be false precision against a 60s tick), so it reuses the row rather than growing a near-identical sibling:

```typescript
export const CACHE_BUDGET_OPTIONS: PresetOption[] = [
  { label: "100 MB", value: 100 },
  { label: "300 MB", value: 300 },
  { label: "1 GB", value: 1024 },
  { label: "Off", value: null },
];
```

This requires one rename inside that file: `SleepTimerRow` → `PresetRow` and `SleepTimerOption.minutes` → `PresetOption.value`, because the row is already unit-agnostic in everything but its names. The alternative — a `CacheBudgetRow` that forwards to the same markup — would be a passthrough that earns nothing.

Label: `Trim cache over`, sentence case per the repo convention.

## Release

Minor bump (new backward-compatible feature) per `AGENTS.md`.
