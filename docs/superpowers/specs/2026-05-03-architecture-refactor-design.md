# Architecture refactor design

Addresses 11 structural issues identified during a full codebase audit. Goal: improve developer experience, reduce duplication, fix data model mismatches, and clean up dead code.

## Decisions

- Delete `@pane/core` (unused, drifted from reality)
- Keep tabs nested in `profileStore`, add `activeProfileId` to `tabStore`
- Extract IPC handlers to an `IpcRouter` class in `ipc.ts`
- Defer tests to a follow-up plan
- Execute bottom-up: foundations first, then data flow, then module refactors

## Phase 1: Cleanup and config fixes

### Fix `base.json` tsconfig defaults

**Problem:** `packages/config-typescript/base.json` defaults to `"module": "NodeNext"` / `"moduleResolution": "NodeNext"`, but every consumer (`@pane/ui`, `@pane/core`, `@pane/config-tsdown`, `@pane/desktop`) overrides to `"bundler"`. The default is wrong for a bundled monorepo.

**Change:** Set `base.json` to `"module": "ESNext"` / `"moduleResolution": "bundler"`. Remove the redundant overrides from all consumer tsconfigs.

**Files:**
- `packages/config-typescript/base.json`
- `packages/ui/tsconfig.json`
- `packages/config-tsdown/tsconfig.json`
- `apps/desktop/tsconfig.json`

### Delete `@pane/core`

**Problem:** `@pane/core` exports zod schemas (`BrowserProfile`, `Fingerprint`, `ProxyConfig`) and a `ProfileStatus` enum. Nothing imports them. The schemas have drifted from the actual runtime types in `profile-store.ts` (e.g., `type` vs `proxyType`, extra fields like `canvas`/`webrtc`). Dead package that misleads new contributors.

**Change:** Delete `packages/core/` entirely. Remove from `knip.json` workspaces. No other references exist.

**Files:**
- Delete `packages/core/` (all files)
- `knip.json` (remove `"packages/core": {}` entry)

### Delete empty `main.js`

**Problem:** `/main.js` is a 0-byte file at the repo root, unreferenced anywhere. Likely scaffolding leftover.

**Change:** Delete the file.

**Files:**
- Delete `main.js`

## Phase 2: Data flow improvements

### Add `activeProfileId` to `tabStore`

**Problem:** No `activeProfileId` exists. Every time the app needs the active profile, it does a linear scan: `for (const profile of profiles) { if (profile.tabs.find(t => t.id === activeTabId)) }`. This appears 6 times in `pane.ts` and 1 time in `address-bar-connected.tsx`.

**Change:** Add `activeProfileId: string | null` to `TabState`. Update `setActiveTab` signature:

```ts
interface TabState {
  activeTabId: string | null;
  activeProfileId: string | null;
  setActiveTab: (tabId: string | null, profileId: string | null) => void;
}
```

Update all call sites that set the active tab to also pass the profile ID:
- `ProfileTabs.open()` — knows `this.profile.id`
- `ProfileTabs.activate()` — knows `this.profile.id`
- `ProfileTabs.close()` — when closing the active tab, picks the last remaining tab across all profiles as the next active. If no tabs remain, passes `null, null`. The next tab's profile ID is determined from `profileStore` by finding which profile owns it.

On the renderer side, `address-bar-connected.tsx` replaces its `for` loop with a direct `profiles.find(p => p.id === activeProfileId)` read from `tabStore`.

**Files:**
- `apps/desktop/src/stores/tab-store.ts`
- `apps/desktop/src/main/profile-tabs.ts`
- `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`

### Deduplicate `serialize` function

**Problem:** `store-sync.ts` and `middlewares/sync.ts` both define identical functions that JSON-stringify state while stripping functions.

**Change:** Extract to `apps/desktop/src/stores/serialize.ts`:

```ts
export function serializeState(state: unknown): string {
  return JSON.stringify(state, (_key, value) =>
    typeof value === "function" ? undefined : value,
  );
}
```

Both `store-sync.ts` and `middlewares/sync.ts` import from there.

**Files:**
- Create `apps/desktop/src/stores/serialize.ts`
- `apps/desktop/src/main/store-sync.ts`
- `apps/desktop/src/stores/middlewares/sync.ts`

## Phase 3: Module refactors

### Split `profile-store.ts` concerns

**Problem:** `profile-store.ts` defines 6 interfaces, a `ProfileColor` enum, a `PROFILE_COLOR_HEX` map, and the store with 6 actions — all in ~160 lines. Presentation constants (color enum, hex map) are mixed with domain types and store logic.

**Change:** Extract `ProfileColor` enum and `PROFILE_COLOR_HEX` to `apps/desktop/src/stores/profile-colors.ts`. Both the store and renderer components import from there.

Domain types (`Fingerprint`, `ProxyConfig`, `Tab`, `BrowserProfile`) stay in `profile-store.ts` colocated with the store.

**Files:**
- Create `apps/desktop/src/stores/profile-colors.ts`
- `apps/desktop/src/stores/profile-store.ts` (imports `ProfileColor` from `profile-colors.ts`)
- `apps/desktop/src/renderer/components/address-bar/address-bar.tsx` (update import)
- `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx` (update import)
- `apps/desktop/src/renderer/components/color-picker.tsx` (update import)
- `apps/desktop/src/renderer/components/create-profile-sheet.tsx` (update import)
- `apps/desktop/src/renderer/components/sidebar/profile-item.tsx` (update import)

### Extract IPC handlers to `IpcRouter`

**Problem:** `Pane.registerIpc()` is 150 lines of repetitive IPC handler registration. 6 handlers repeat the same "find profile for active tab" loop. IPC infrastructure is mixed with domain logic.

**Change:** Create `apps/desktop/src/main/ipc.ts` with an `IpcRouter` class:

```ts
export class IpcRouter {
  constructor(private readonly pane: Pane) {}

  register(): void {
    // all ipcMain.handle calls
  }

  private findProfileForActiveTab(): Profile | undefined {
    const { activeProfileId } = tabStore.getState();
    return activeProfileId ? this.pane.getProfile(activeProfileId) : undefined;
  }
}
```

- `findProfileForActiveTab()` is O(1) using the new `activeProfileId` from Phase 2.
- Remove `registerIpc()` from `Pane`.
- In `index.ts`, replace `pane.registerIpc()` with `new IpcRouter(pane).register()`.

**Files:**
- Create `apps/desktop/src/main/ipc.ts`
- `apps/desktop/src/main/pane.ts` (remove `registerIpc()` method)
- `apps/desktop/src/main/index.ts` (use `IpcRouter`)

### Extract hardcoded fingerprints

**Problem:** `create-profile-sheet.tsx` contains a 50-line `FINGERPRINTS` record with hardcoded browser fingerprint data per platform. Domain data embedded in a form component.

**Change:** Move to `apps/desktop/src/stores/default-fingerprints.ts`. Export as `DEFAULT_FINGERPRINTS`. The component imports it.

**Files:**
- Create `apps/desktop/src/stores/default-fingerprints.ts`
- `apps/desktop/src/renderer/components/create-profile-sheet.tsx` (update import)

## Phase 4: Remaining cleanup

### Fix `require()` in `fs-storage.ts`

**Problem:** `fs-storage.ts` uses runtime `require("electron")`, `require("node:fs")`, `require("node:path")` inside function bodies. This is because the file is imported by both main and renderer processes. The renderer-side guard prevents execution, but `require()` in an ESM codebase is a code smell.

**Change:** Replace with static ESM imports at the top of the file:

```ts
import { app } from "electron";
import fs from "node:fs";
import path from "node:path";
```

The renderer-side guards remain — `getItem`/`setItem`/`removeItem` short-circuit before hitting any Electron or Node API. electron-vite handles the bundling correctly: the main process resolves the imports, the renderer tree-shakes the dead code paths.

If the renderer build fails on the `electron` import, fall back to keeping `require("electron")` only for `app.getPath()` while using static imports for `node:fs` and `node:path`.

**Files:**
- `apps/desktop/src/stores/middlewares/fs-storage.ts`

## Verification

After all phases, run the full quality gate:

```
bun turbo run build && bun turbo run typecheck && bun eslint && bun biome check --max-diagnostics 500 && bun knip
```

All checks must pass clean.
