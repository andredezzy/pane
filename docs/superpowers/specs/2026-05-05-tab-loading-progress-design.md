# Tab Loading Progress Indicator

## Overview

Add a loading progress indicator to browser tabs. A 2px simulated progress bar renders at the bottom edge of the address input. The reload button becomes a stop button during loading.

## Main Process — Loading Events

In `profile-tabs.ts`, register two new `webContents` event listeners in `createView()`:

- `did-start-loading` → call `tabStore.getState().setLoading(tabId, true)`
- `did-stop-loading` → call `tabStore.getState().setLoading(tabId, false)`

Add a `tabs.stop` tRPC procedure that calls `webContents.stop()` on the active tab.

## Store — Loading State

In `tab-store.ts`, add runtime-only loading state:

- `loadingTabIds: string[]` — array of tab IDs currently loading
- `setLoading(tabId: string, isLoading: boolean)` — adds/removes from the array

This state is runtime-only (not persisted). It syncs to the renderer via the existing sync middleware.

## Renderer — Progress Bar

### ToolbarAddress

Wrap the address `<input>` in a relative-positioned container. Add an absolutely-positioned `<div>` at `bottom: 0; left: 0; height: 2px` inside it.

Remove the `border` from the address input. Keep only the background (`rgba(255,255,255,0.03)`) and the `border-radius: 5px`. The progress bar clips to the input's border radius via `overflow: hidden` on the container.

### Progress Animation

The progress bar uses a CSS-driven simulated progress with three phases:

1. **Loading starts** → animate width from `0%` to `80%` over ~2s with `ease-out`
2. **Crawl** → slowly animate from `80%` to `92%` over ~8s with `linear`
3. **Loading finishes** (`isLoading` becomes `false`) → snap to `100%` over 200ms, then fade out opacity over 200ms, then reset to idle

Color: blue gradient `#3b82f6` → `#60a5fa`.

### ToolbarNavigationReload

When the active tab is loading:
- Show `X` icon (from lucide-react) instead of `RotateCw`
- `onClick` calls `trpc.tabs.stop.mutate()` instead of `trpc.tabs.reload.mutate()`

## Files to Modify

1. `apps/desktop/src/stores/tab-store.ts` — add `loadingTabIds` and `setLoading`
2. `apps/desktop/src/main/profile/profile-tabs.ts` — add `did-start-loading` / `did-stop-loading` listeners
3. `apps/desktop/src/main/trpc/routers/tabs.ts` — add `tabs.stop` procedure
4. `apps/desktop/src/renderer/pages/browser/_components/toolbar.tsx` — remove border from `ToolbarAddress`, add progress bar element, update `ToolbarNavigationReload` to accept `loading` prop
5. `apps/desktop/src/renderer/pages/browser/index.tsx` — read loading state from `tabStore`, pass to toolbar components
