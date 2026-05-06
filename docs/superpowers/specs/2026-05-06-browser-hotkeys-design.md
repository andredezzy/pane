# Browser Hotkeys Design

## Goal

Add standard browser keyboard shortcuts to the Electron app, including an Arc-style MRU tab switcher overlay triggered by Ctrl+Tab.

## Architecture

All hotkeys are registered as Electron menu accelerators in `createMenu()`. This works regardless of which `WebContentsView` has focus. Hotkeys that need to notify the renderer (address bar focus, MRU overlay) use a tRPC subscription to push events from main to renderer, consistent with the existing "all IPC goes through tRPC" pattern.

## Hotkey Map

| Shortcut | Action | Where |
|---|---|---|
| `Cmd+R` | Reload active tab | Main process direct |
| `Cmd+[` | Go back | Main process direct |
| `Cmd+]` | Go forward | Main process direct |
| `Cmd+T` | New tab in active profile | Main process direct |
| `Cmd+W` | Close active tab | Main process direct |
| `Cmd+Shift+T` | Reopen last closed tab | Main process direct |
| `Cmd+L` | Focus address bar | tRPC subscription event → renderer |
| `Cmd+1`–`Cmd+9` | Switch to tab N in active profile | Main process direct |
| `Ctrl+Tab` | MRU overlay forward | tRPC subscription event → renderer |
| `Ctrl+Shift+Tab` | MRU overlay backward | tRPC subscription event → renderer |
| `Escape` (address bar) | Unfocus address bar, reset URL | Renderer-only `onKeyDown` |

## Menu Registration

`createMenu()` signature changes from `getActiveTabContents` callback to receiving the `Pane` instance and the chrome `WebContents` reference directly. `Pane` already has `getActiveTabContents()`, plus access to tab operations via stores and profile methods.

For renderer-bound events, the menu click callback emits to a `HotkeyEmitter` (a simple typed `EventEmitter`). A tRPC subscription (`hotkeys.events`) yields from this emitter, and the renderer subscribes on mount.

### Existing menu changes

- The `reload` role item currently reloads the chrome renderer. Replace it with a custom `Cmd+R` accelerator that reloads the active browser tab instead.
- The existing `Cmd+Option+I` (chrome DevTools) and `Cmd+Shift+I` (tab DevTools) remain unchanged.

### Edge cases

- **Cmd+T with no active profile:** No-op. A profile must be selected in the sidebar first.
- **Cmd+W on the last tab in a profile:** Closes the tab. The profile remains open with no active tab (empty state).
- **Cmd+Shift+T when closed stack is empty:** No-op.
- **Cmd+1–9 exceeds tab count:** No-op if the profile has fewer tabs than the number pressed.
- **Hotkeys while on Settings page:** Navigation hotkeys (Cmd+R, Cmd+[, Cmd+]) are no-ops since there's no active tab. Cmd+T still works (opens tab and navigates to browser). Ctrl+Tab overlay still works (switches to a tab and navigates to browser).

## State Management

### MRU History

Added to `tab-store` as `mruHistory: string[]` — tab IDs ordered most-recent-first.

Updated when:
- **Tab activated** — move to front of list
- **Tab opened** — insert at front
- **Tab closed** — remove from list

Cross-profile by design — it stores tab IDs only, and `tab-store` already manages `activeTabId`/`activeProfileId` globally.

Not persisted — resets on app restart. Initialized from current tab order on startup.

### Closed Tabs Stack

Added to `tab-store` as `closedTabs: ClosedTab[]`.

```typescript
interface ClosedTab {
  url: string
  profileId: string
  title?: string
  favicon?: string
}
```

- **Push** when a tab is closed — captured before the `WebContentsView` is destroyed
- **Pop** on `Cmd+Shift+T` — reopens in the original profile with the saved URL
- Capped at 20 entries
- Not persisted — clears on app restart

## MRU Overlay

### Trigger Flow

1. `Ctrl+Tab` menu accelerator fires → emits `TAB_SWITCHER_FORWARD` to `HotkeyEmitter`
2. tRPC subscription yields event to renderer
3. Renderer shows overlay, selects 2nd item (the previous tab)
4. Subsequent `Ctrl+Tab` → same flow → renderer cycles forward through the list
5. `Ctrl+Shift+Tab` → same flow with `TAB_SWITCHER_BACKWARD` → renderer cycles backward
6. User releases `Ctrl` → renderer `keyup` listener on `Control` key → confirms selection, hides overlay, switches to selected tab via `trpc.tabs.switch`
7. `Escape` → cancel, hide overlay, stay on current tab

### Overlay UI

- Centered floating panel, rendered in `Layout` above everything (`z-50+`)
- Backdrop with subtle dim/blur
- Vertical list of up to 8 most recently used tabs
- Each row: profile color dot + favicon + tab title
- Selected row visually highlighted
- No page screenshot previews — favicon + title only

### Component

`TabSwitcher` component rendered in `Layout`, controlled by local state. The tRPC subscription handler toggles visibility and cycles selection. A `keyup` listener on the `window` detects `Control` release to confirm.

## Address Bar Focus

### Cmd+L Flow

1. Menu accelerator fires → emits `FOCUS_ADDRESS_BAR` to `HotkeyEmitter`
2. tRPC subscription yields event to renderer
3. Renderer calls `.focus()` and `.select()` on the address bar input ref
4. `ToolbarAddress` already uses `forwardRef` — `BrowserPage` holds the ref and registers the subscription handler

### Escape to Unfocus

- `onKeyDown` handler on the `ToolbarAddress` input
- On `Escape`: blur the input, reset value to the current tab URL
- Renderer-only — no menu accelerator or IPC needed

## tRPC Hotkey Router

New router at `src/main/trpc/routers/hotkeys.ts`:

```typescript
enum HotkeyEvent {
  FOCUS_ADDRESS_BAR = "FOCUS_ADDRESS_BAR",
  TAB_SWITCHER_FORWARD = "TAB_SWITCHER_FORWARD",
  TAB_SWITCHER_BACKWARD = "TAB_SWITCHER_BACKWARD",
}
```

Single subscription procedure `hotkeys.events` that yields `HotkeyEvent` values from the `HotkeyEmitter`. The emitter is instantiated once and passed to both `createMenu()` and the tRPC context.

## Hotkey Emitter

A typed `EventEmitter` in `src/main/hotkey-emitter.ts`:

```typescript
import { EventEmitter } from "node:events";

export enum HotkeyEvent {
  FOCUS_ADDRESS_BAR = "FOCUS_ADDRESS_BAR",
  TAB_SWITCHER_FORWARD = "TAB_SWITCHER_FORWARD",
  TAB_SWITCHER_BACKWARD = "TAB_SWITCHER_BACKWARD",
}

class HotkeyEmitter extends EventEmitter {
  emit(event: HotkeyEvent): boolean {
    return super.emit("hotkey", event);
  }

  on(callback: (event: HotkeyEvent) => void): this {
    return super.on("hotkey", callback);
  }
}
```

Instantiated in `index.ts`, passed to `createMenu()` and to the tRPC context factory.

## File Changes Summary

### New files
- `src/main/hotkey-emitter.ts` — typed emitter
- `src/main/trpc/routers/hotkeys.ts` — tRPC hotkey router with subscription
- `src/renderer/components/tab-switcher.tsx` — MRU overlay component

### Modified files
- `src/main/window.ts` — expand `createMenu()` to register all accelerators
- `src/main/index.ts` — instantiate `HotkeyEmitter`, pass to menu and tRPC context
- `src/main/trpc/router.ts` — register `hotkeys` router
- `src/main/trpc/routers/tabs.ts` — capture closed tab data before closing, update MRU on operations
- `src/main/profile/profile-tabs.ts` — capture URL/title/favicon before destroying tab, update MRU history
- `src/stores/tab-store.ts` — add `mruHistory`, `closedTabs`, and their mutation methods
- `src/renderer/app/layout.tsx` — render `TabSwitcher`, subscribe to hotkey events
- `src/renderer/app/browser/page.tsx` — hold address bar ref, handle focus event, add Escape handler
- `src/renderer/app/browser/_components/toolbar.tsx` — forward ref properly for focus, add Escape onKeyDown
