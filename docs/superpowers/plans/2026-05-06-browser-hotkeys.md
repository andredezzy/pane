# Browser Hotkeys Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add standard browser keyboard shortcuts (Cmd+W, Cmd+T, Cmd+R, Cmd+L, Cmd+[, Cmd+], Cmd+1-9, Cmd+Shift+T) and an Arc-style MRU tab switcher overlay (Ctrl+Tab) to the Electron app.

**Architecture:** All hotkeys registered as Electron menu accelerators in `createMenu()` — works regardless of which WebContentsView has focus. Renderer-bound events (focus address bar, MRU overlay) pushed via tRPC subscription from a `HotkeyEmitter`. MRU history and closed-tabs stack tracked in `tab-store`.

**Tech Stack:** Electron menu accelerators, tRPC subscriptions (async generators), Zustand vanilla stores, React

---

## File Structure

### New files
| File | Responsibility |
|---|---|
| `src/main/hotkey-emitter.ts` | Typed EventEmitter for hotkey events |
| `src/main/trpc/routers/hotkeys.ts` | tRPC router with `events` subscription |
| `src/renderer/components/tab-switcher.tsx` | MRU overlay component |

### Modified files
| File | Changes |
|---|---|
| `src/stores/tab-store.ts` | Add `mruHistory`, `closedTabs`, mutation methods |
| `src/main/profile/profile-tabs.ts` | Update MRU on activate/open, capture closed tab data |
| `src/main/trpc/routers/tabs.ts` | Wire MRU updates and closed-tab capture into close/switch/open mutations |
| `src/main/trpc/router.ts` | Register `hotkeys` router |
| `src/main/trpc/trpc.ts` | Add `hotkeyEmitter` to Context |
| `src/main/window.ts` | Expand `createMenu()` with all accelerators |
| `src/main/index.ts` | Instantiate `HotkeyEmitter`, pass to menu and context |
| `src/renderer/app/layout.tsx` | Render `TabSwitcher`, subscribe to hotkey events, forward address bar focus |
| `src/renderer/app/browser/page.tsx` | Hold address bar ref, handle focus/escape |
| `src/renderer/app/browser/_components/toolbar.tsx` | Add `onKeyDown` prop forwarding for Escape |

---

### Task 1: Add MRU History and Closed Tabs to Tab Store

**Files:**
- Modify: `apps/veil/src/stores/tab-store.ts`

- [ ] **Step 1: Add `ClosedTab` interface and new state fields to `TabState`**

In `apps/veil/src/stores/tab-store.ts`, add the `ClosedTab` interface and expand the `TabState` interface with new fields and methods:

```typescript
import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

const MAX_CLOSED_TABS = 20;

export interface ClosedTab {
  url: string;
  profileId: string;
  title?: string;
  favicon?: string;
}

export interface TabState {
  activeTabId: string | null;
  activeProfileId: string | null;
  loadingTabIds: string[];
  mruHistory: string[];
  closedTabs: ClosedTab[];

  setActiveTab: (tabId: string | null, profileId: string | null) => void;
  setLoading: (tabId: string, isLoading: boolean) => void;
  pushMru: (tabId: string) => void;
  removeMru: (tabId: string) => void;
  pushClosedTab: (tab: ClosedTab) => void;
  popClosedTab: () => ClosedTab | undefined;
}
```

- [ ] **Step 2: Implement the new store methods**

Replace the store creator in `apps/veil/src/stores/tab-store.ts` with:

```typescript
export const tabStore = createStore<TabState>()(
  sync(
    (set, get) => ({
      activeTabId: null,
      activeProfileId: null,
      loadingTabIds: [],
      mruHistory: [],
      closedTabs: [],

      setActiveTab: (tabId, profileId) =>
        set({ activeTabId: tabId, activeProfileId: profileId }),

      setLoading: (tabId, isLoading) =>
        set((state) => {
          if (isLoading) {
            if (state.loadingTabIds.includes(tabId)) {
              return state;
            }

            return { loadingTabIds: [...state.loadingTabIds, tabId] };
          }

          return {
            loadingTabIds: state.loadingTabIds.filter((id) => id !== tabId),
          };
        }),

      pushMru: (tabId) =>
        set((state) => ({
          mruHistory: [
            tabId,
            ...state.mruHistory.filter((id) => id !== tabId),
          ],
        })),

      removeMru: (tabId) =>
        set((state) => ({
          mruHistory: state.mruHistory.filter((id) => id !== tabId),
        })),

      pushClosedTab: (tab) =>
        set((state) => ({
          closedTabs: [tab, ...state.closedTabs].slice(0, MAX_CLOSED_TABS),
        })),

      popClosedTab: () => {
        const { closedTabs } = get();

        if (closedTabs.length === 0) {
          return undefined;
        }

        const [first, ...rest] = closedTabs;
        set({ closedTabs: rest });

        return first;
      },
    }),
    { name: "tab-store" },
  ),
);
```

- [ ] **Step 3: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: No type errors

- [ ] **Step 4: Commit**

```bash
git add apps/veil/src/stores/tab-store.ts
git commit -m "feat: add MRU history and closed tabs stack to tab-store"
```

---

### Task 2: Wire MRU and Closed Tab Tracking into Profile Tabs

**Files:**
- Modify: `apps/veil/src/main/profile/profile-tabs.ts`

The `ProfileTabs` class manages tab lifecycle. We need to:
1. Call `tabStore.getState().pushMru(tabId)` when a tab is activated or opened
2. Call `tabStore.getState().removeMru(tabId)` and `tabStore.getState().pushClosedTab(...)` when a tab is closed (capturing data before destruction)

- [ ] **Step 1: Update `activate()` to push MRU**

In `apps/veil/src/main/profile/profile-tabs.ts`, find the `activate` method. At line 110 where `tabStore.getState().setActiveTab(tabId, this.profile.id)` is called, add a `pushMru` call right after:

```typescript
  activate(tabId: string): void {
    let view = this.views.get(tabId);

    if (!view) {
      const tab = this.profile.data.tabs.find((tab) => tab.id === tabId);

      if (tab) {
        view = this.createView(tabId);
        this.views.set(tabId, view);
        this.applyFingerprint(view.webContents, this.profile.data.fingerprint);
        view.webContents.loadURL(tab.url);
      }
    }

    if (view) {
      if (!this.mainWindow.contentView.children.includes(view)) {
        this.mainWindow.contentView.addChildView(view);
        this.profile.ece.addTab(view.webContents, this.mainWindow);
        this.profile.extensions.ensureLoaded();
      }

      view.setVisible(true);
      this.profile.ece.selectTab(view.webContents);
    }

    tabStore.getState().setActiveTab(tabId, this.profile.id);
    tabStore.getState().pushMru(tabId);
  }
```

- [ ] **Step 2: Update `close()` to capture closed tab data and remove from MRU**

In `apps/veil/src/main/profile/profile-tabs.ts`, update the `close` method. Before the view is destroyed, capture the tab's URL/title/favicon from `profileStore`, then push to closed tabs and remove from MRU:

```typescript
  close(tabId: string): void {
    const view = this.views.get(tabId);

    if (!view) {
      return;
    }

    const tabData = this.profile.data.tabs.find((tab) => tab.id === tabId);

    if (tabData && tabData.url) {
      tabStore.getState().pushClosedTab({
        url: tabData.url,
        profileId: this.profile.id,
        title: tabData.title || undefined,
        favicon: tabData.favicon || undefined,
      });
    }

    this.profile.ece.removeTab(view.webContents);
    this.mainWindow.contentView.removeChildView(view);
    view.webContents.close();

    this.views.delete(tabId);
    tabStore.getState().setLoading(tabId, false);
    tabStore.getState().removeMru(tabId);

    this.profile.onTabClosed(tabId);
    profileStore.getState().closeTab(this.profile.id, tabId);

    if (tabStore.getState().activeTabId === tabId) {
      const remainingTabs = this.profile.data.tabs;
      const nextTab = remainingTabs[remainingTabs.length - 1];

      if (nextTab) {
        this.activate(nextTab.id);
      } else {
        tabStore.getState().setActiveTab(null, null);
      }
    }
  }
```

- [ ] **Step 3: Update `open()` to push MRU**

The `open` method already calls `this.activate(id)` at the end, which now calls `pushMru`. No additional change needed — verify this is the case by reading the method. The `activate` call at line 51 (`this.activate(id)`) handles it.

- [ ] **Step 4: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: No type errors

- [ ] **Step 5: Commit**

```bash
git add apps/veil/src/main/profile/profile-tabs.ts
git commit -m "feat: track MRU history and closed tabs in profile-tabs lifecycle"
```

---

### Task 3: Create Hotkey Emitter

**Files:**
- Create: `apps/veil/src/main/hotkey-emitter.ts`

- [ ] **Step 1: Create the typed emitter**

Create `apps/veil/src/main/hotkey-emitter.ts`:

```typescript
import { EventEmitter } from "node:events";

export enum HotkeyEvent {
  FOCUS_ADDRESS_BAR = "FOCUS_ADDRESS_BAR",
  TAB_SWITCHER_FORWARD = "TAB_SWITCHER_FORWARD",
  TAB_SWITCHER_BACKWARD = "TAB_SWITCHER_BACKWARD",
}

const CHANNEL = "hotkey";

export class HotkeyEmitter extends EventEmitter {
  emitHotkey(event: HotkeyEvent): void {
    this.emit(CHANNEL, event);
  }

  onHotkey(callback: (event: HotkeyEvent) => void): void {
    this.on(CHANNEL, callback);
  }

  offHotkey(callback: (event: HotkeyEvent) => void): void {
    this.off(CHANNEL, callback);
  }
}
```

- [ ] **Step 2: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: No type errors

- [ ] **Step 3: Commit**

```bash
git add apps/veil/src/main/hotkey-emitter.ts
git commit -m "feat: create typed HotkeyEmitter for menu-to-renderer events"
```

---

### Task 4: Create Hotkey tRPC Router

**Files:**
- Create: `apps/veil/src/main/trpc/routers/hotkeys.ts`
- Modify: `apps/veil/src/main/trpc/trpc.ts`
- Modify: `apps/veil/src/main/trpc/router.ts`

- [ ] **Step 1: Add `hotkeyEmitter` to the tRPC Context**

In `apps/veil/src/main/trpc/trpc.ts`, add the `HotkeyEmitter` to the `Context` interface:

```typescript
import { initTRPC } from "@trpc/server";
import type { BrowserWindow } from "electron";
import type { StoreApi } from "zustand/vanilla";

import type { StoreName } from "../../stores/middlewares/sync";
import type { HotkeyEmitter } from "../hotkey-emitter";
import type { Pane } from "../pane";

export type { StoreName };

export interface Context {
  pane: Pane;
  stores: Record<StoreName, StoreApi<object>>;
  surface: BrowserWindow;
  hotkeyEmitter: HotkeyEmitter;
}

const t = initTRPC.context<Context>().create({ isServer: true });

export const router = t.router;
export const procedure = t.procedure;
```

- [ ] **Step 2: Create the hotkeys router**

Create `apps/veil/src/main/trpc/routers/hotkeys.ts`:

```typescript
import { HotkeyEvent } from "../../hotkey-emitter";
import { procedure, router } from "../trpc";

export { HotkeyEvent };

export const hotkeysRouter = router({
  events: procedure.subscription(async function* ({ ctx, signal }) {
    const queue: HotkeyEvent[] = [];
    let resolve: (() => void) | null = null;

    const handler = (event: HotkeyEvent) => {
      queue.push(event);

      if (resolve) {
        resolve();
        resolve = null;
      }
    };

    ctx.hotkeyEmitter.onHotkey(handler);

    try {
      while (!signal?.aborted) {
        if (queue.length === 0) {
          await new Promise<void>((r) => {
            resolve = r;
          });
        }

        while (queue.length > 0) {
          yield queue.shift()!;
        }
      }
    } finally {
      ctx.hotkeyEmitter.offHotkey(handler);
    }
  }),
});
```

- [ ] **Step 3: Register the hotkeys router**

In `apps/veil/src/main/trpc/router.ts`, add the import and register:

```typescript
import { cwsRouter } from "./routers/cws";
import { extensionsRouter } from "./routers/extensions";
import { hotkeysRouter } from "./routers/hotkeys";
import { profilesRouter } from "./routers/profiles";
import { securityRouter } from "./routers/security";
import { settingsRouter } from "./routers/settings";
import { storesRouter } from "./routers/stores";
import { tabsRouter } from "./routers/tabs";
import { uiRouter } from "./routers/ui";
import { router } from "./trpc";

export const appRouter = router({
  tabs: tabsRouter,
  profiles: profilesRouter,
  settings: settingsRouter,
  security: securityRouter,
  extensions: extensionsRouter,
  cws: cwsRouter,
  stores: storesRouter,
  ui: uiRouter,
  hotkeys: hotkeysRouter,
});

export type AppRouter = typeof appRouter;
```

- [ ] **Step 4: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: Type error in `index.ts` because `createContext` does not include `hotkeyEmitter` yet — this is expected and will be fixed in Task 6.

- [ ] **Step 5: Commit**

```bash
git add apps/veil/src/main/trpc/routers/hotkeys.ts apps/veil/src/main/trpc/trpc.ts apps/veil/src/main/trpc/router.ts
git commit -m "feat: add hotkeys tRPC router with events subscription"
```

---

### Task 5: Expand Menu with All Accelerators

**Files:**
- Modify: `apps/veil/src/main/window.ts`

- [ ] **Step 1: Update `createMenu()` signature and add all accelerators**

Replace the entire `createMenu` function in `apps/veil/src/main/window.ts`. The function now receives `Pane`, `HotkeyEmitter`, and the chrome `WebContentsView`:

```typescript
import path from "node:path";
import {
  app,
  BaseWindow,
  BrowserWindow,
  Menu,
  nativeImage,
  WebContentsView,
} from "electron";

import type { HotkeyEmitter } from "./hotkey-emitter";
import { HotkeyEvent } from "./hotkey-emitter";
import type { Pane } from "./pane";
```

Add the imports above, then replace `createMenu`:

```typescript
export function createMenu(
  chrome: WebContentsView,
  pane: Pane,
  hotkeyEmitter: HotkeyEmitter,
): void {
  const menu = Menu.buildFromTemplate([
    { role: "appMenu" },
    {
      label: "File",
      submenu: [
        {
          label: "New tab",
          accelerator: "CommandOrControl+T",
          click: () => {
            const { activeProfileId } =
              pane.tabStore.getState();

            if (activeProfileId) {
              pane.hideAllTabs();
              pane.getOrCreateProfile(activeProfileId).tabs.open();
              pane.navigateToBrowser();
            }
          },
        },
        {
          label: "Close tab",
          accelerator: "CommandOrControl+W",
          click: () => {
            const { activeTabId } = pane.tabStore.getState();

            if (activeTabId) {
              pane.getProfileForTab(activeTabId)?.tabs.close(activeTabId);
            }
          },
        },
        {
          label: "Reopen closed tab",
          accelerator: "CommandOrControl+Shift+T",
          click: () => {
            const closedTab = pane.tabStore.getState().popClosedTab();

            if (closedTab) {
              pane.hideAllTabs();
              pane
                .getOrCreateProfile(closedTab.profileId)
                .tabs.open(closedTab.url);
              pane.navigateToBrowser();
            }
          },
        },
        { type: "separator" },
        { role: "close" },
      ],
    },
    { role: "editMenu" },
    {
      label: "View",
      submenu: [
        {
          label: "Reload tab",
          accelerator: "CommandOrControl+R",
          click: () => pane.getActiveTabContents()?.reload(),
        },
        { type: "separator" },
        {
          label: "Toggle developer tools",
          accelerator: "CommandOrControl+Option+I",
          click: () => chrome.webContents.openDevTools({ mode: "detach" }),
        },
        {
          label: "Toggle page developer tools",
          accelerator: "CommandOrControl+Shift+I",
          click: () =>
            pane.getActiveTabContents()?.openDevTools({ mode: "detach" }),
        },
        { type: "separator" },
        { role: "resetZoom" },
        { role: "zoomIn" },
        { role: "zoomOut" },
        { type: "separator" },
        { role: "togglefullscreen" },
      ],
    },
    {
      label: "Navigate",
      submenu: [
        {
          label: "Back",
          accelerator: "CommandOrControl+[",
          click: () => pane.getActiveTabContents()?.goBack(),
        },
        {
          label: "Forward",
          accelerator: "CommandOrControl+]",
          click: () => pane.getActiveTabContents()?.goForward(),
        },
        { type: "separator" },
        {
          label: "Focus address bar",
          accelerator: "CommandOrControl+L",
          click: () => hotkeyEmitter.emitHotkey(HotkeyEvent.FOCUS_ADDRESS_BAR),
        },
      ],
    },
    {
      label: "Tab",
      submenu: [
        {
          label: "Next tab (MRU)",
          accelerator: "Control+Tab",
          click: () =>
            hotkeyEmitter.emitHotkey(HotkeyEvent.TAB_SWITCHER_FORWARD),
        },
        {
          label: "Previous tab (MRU)",
          accelerator: "Control+Shift+Tab",
          click: () =>
            hotkeyEmitter.emitHotkey(HotkeyEvent.TAB_SWITCHER_BACKWARD),
        },
        { type: "separator" },
        ...Array.from({ length: 9 }, (_, index) => ({
          label: `Tab ${index + 1}`,
          accelerator: `CommandOrControl+${index + 1}`,
          click: () => pane.switchToTabByIndex(index),
        })),
      ],
    },
    { role: "windowMenu" },
  ]);

  Menu.setApplicationMenu(menu);
}
```

- [ ] **Step 2: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: Type errors for `pane.tabStore`, `pane.navigateToBrowser()`, and `pane.switchToTabByIndex()` — these don't exist yet. They will be added in Task 6.

- [ ] **Step 3: Commit**

```bash
git add apps/veil/src/main/window.ts
git commit -m "feat: register all browser hotkey accelerators in native menu"
```

---

### Task 6: Update Pane Class and Wire Everything in Index

**Files:**
- Modify: `apps/veil/src/main/pane.ts`
- Modify: `apps/veil/src/main/index.ts`

- [ ] **Step 1: Add helper methods to Pane**

In `apps/veil/src/main/pane.ts`, add public accessors for `tabStore` and two new helper methods. Add the `tabStore` import and expose it, plus `navigateToBrowser` and `switchToTabByIndex`:

At the top of the file, add the import:

```typescript
import path from "node:path";
import { app, type BaseWindow, type WebContents } from "electron";

import { navigationStore, Page } from "../stores/navigation-store";
import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { autoDetectBrowser } from "./detect-browser";
import { ExtensionInstaller } from "./extension-installer";
import { Profile } from "./profile/profile";
```

Inside the `Pane` class, add a public getter and two methods. After the `readonly profiles` line add:

```typescript
  readonly tabStore = tabStore;
```

After the `hideAllTabs()` method, add:

```typescript
  navigateToBrowser(): void {
    navigationStore.getState().navigate(Page.BROWSER);
  }

  switchToTabByIndex(index: number): void {
    const { activeProfileId } = tabStore.getState();

    if (!activeProfileId) {
      return;
    }

    const profile = profileStore
      .getState()
      .profiles.find((profile) => profile.id === activeProfileId);

    if (!profile) {
      return;
    }

    const tab = profile.tabs[index];

    if (!tab) {
      return;
    }

    this.hideAllTabs();
    this.getOrCreateProfile(activeProfileId).tabs.activate(tab.id);
    this.navigateToBrowser();
  }
```

- [ ] **Step 2: Update `index.ts` to instantiate `HotkeyEmitter` and wire it in**

In `apps/veil/src/main/index.ts`, add the import and wire the emitter:

Add to imports:

```typescript
import { HotkeyEmitter } from "./hotkey-emitter";
```

In the `setup()` function, instantiate the emitter, update `createMenu` call, and add it to context:

```typescript
function setup() {
  appWindow = createAppWindow();

  const currentPane = new Pane(appWindow.mainWindow);
  pane = currentPane;

  const hotkeyEmitter = new HotkeyEmitter();

  createMenu(appWindow.chrome, currentPane, hotkeyEmitter);

  const createContext = () => ({
    pane: currentPane,
    surface: appWindow?.surface as BrowserWindow,
    hotkeyEmitter,
    stores: {
      "profile-store": profileStore,
      "tab-store": tabStore,
      "navigation-store": navigationStore,
      "settings-store": settingsStore,
      "extension-store": extensionStore,
      "security-store": securityStore,
    },
  });

  // ... rest unchanged
```

- [ ] **Step 3: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: No type errors — all pieces now connected

- [ ] **Step 4: Commit**

```bash
git add apps/veil/src/main/pane.ts apps/veil/src/main/index.ts
git commit -m "feat: wire HotkeyEmitter into Pane, menu, and tRPC context"
```

---

### Task 7: Address Bar Focus and Escape Handling

**Files:**
- Modify: `apps/veil/src/renderer/app/browser/page.tsx`
- Modify: `apps/veil/src/renderer/app/browser/_components/toolbar.tsx`

- [ ] **Step 1: Add `onKeyDown` forwarding to `ToolbarAddress`**

In `apps/veil/src/renderer/app/browser/_components/toolbar.tsx`, the `ToolbarAddress` already uses `forwardRef` and spreads `...props`, which means `onKeyDown` is already forwarded through `InputHTMLAttributes`. No change needed to the component itself.

- [ ] **Step 2: Add ref and Escape handling in `BrowserToolbar`**

In `apps/veil/src/renderer/app/browser/page.tsx`, add a ref for the address bar input and handle Escape and the focus subscription. Update the imports and `BrowserToolbar` component:

```typescript
import { useCallback, useEffect, useRef, useState } from "react";
import { useStore } from "zustand/react";
import { useShallow } from "zustand/react/shallow";

import {
  ProfileColor,
  type ProfileColor as ProfileColorType,
} from "../../../constants/profile-colors";
import { extensionStore } from "../../../stores/extension-store";
import { profileStore } from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import { HotkeyEvent } from "../../hooks/use-hotkey-events";
import { trpc } from "../../trpc";
import { BrowserActionList } from "./_components/browser-action-list";
import { EmptyState } from "./_components/empty-state";
import {
  Toolbar,
  ToolbarAddress,
  ToolbarExtensions,
  ToolbarNavigation,
  ToolbarNavigationBack,
  ToolbarNavigationForward,
  ToolbarNavigationReload,
  ToolbarProfile,
} from "./_components/toolbar";
```

Update `BrowserToolbar` to add the ref, Escape handling, and expose a focus method:

```typescript
function BrowserToolbar({
  addressBarRef,
}: {
  addressBarRef: React.RefObject<HTMLInputElement | null>;
}) {
  const { activeTabId, activeProfileId } = useStore(
    tabStore,
    useShallow((state) => ({
      activeTabId: state.activeTabId,
      activeProfileId: state.activeProfileId,
    })),
  );

  const loadingTabIds = useStore(tabStore, (state) => state.loadingTabIds);

  const profiles = useStore(profileStore, (state) => state.profiles);
  const extensions = useStore(extensionStore, (state) => state.extensions);

  const activeProfile = profiles.find(
    (profile) => profile.id === activeProfileId,
  );

  const activeTab = activeProfile?.tabs.find((tab) => tab.id === activeTabId);

  const activeUrl = activeTab?.url ?? "";
  const profileName = activeProfile?.name ?? "";

  const profileColor: ProfileColorType =
    activeProfile?.color ?? ProfileColor.BLUE;

  const profileExtensions = activeProfileId
    ? extensions[activeProfileId]
    : undefined;

  const [inputValue, setInputValue] = useState("");
  const [isFocused, setIsFocused] = useState(false);

  const displayUrl = isFocused ? inputValue : activeUrl;

  const isLoading = activeTabId ? loadingTabIds.includes(activeTabId) : false;

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();

    if (inputValue.trim()) {
      trpc.tabs.navigate.mutate({ url: inputValue.trim() });
      addressBarRef.current?.blur();
    }
  };

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent<HTMLInputElement>) => {
      if (e.key === "Escape") {
        addressBarRef.current?.blur();
      }
    },
    [addressBarRef],
  );

  if (!activeTabId) {
    return null;
  }

  return (
    <Toolbar>
      <ToolbarNavigation>
        <ToolbarNavigationBack onClick={() => trpc.tabs.goBack.mutate()} />
        <ToolbarNavigationForward
          onClick={() => trpc.tabs.goForward.mutate()}
        />
        <ToolbarNavigationReload
          isLoading={isLoading}
          onClick={() =>
            isLoading ? trpc.tabs.stop.mutate() : trpc.tabs.reload.mutate()
          }
        />
      </ToolbarNavigation>

      <form onSubmit={handleSubmit} className="flex flex-1">
        <ToolbarAddress
          ref={addressBarRef}
          isLoading={isLoading}
          value={displayUrl}
          onChange={(e) => setInputValue(e.target.value)}
          onFocus={() => {
            setInputValue(activeUrl);
            setIsFocused(true);
          }}
          onBlur={() => setIsFocused(false)}
          onKeyDown={handleKeyDown}
          placeholder="Search or enter URL"
        />
      </form>

      <ToolbarExtensions>
        {profileExtensions && profileExtensions.length > 0 && (
          <BrowserActionList partition={`persist:profile-${activeProfileId}`} />
        )}
      </ToolbarExtensions>

      {profileName ? (
        <ToolbarProfile color={profileColor}>{profileName}</ToolbarProfile>
      ) : null}
    </Toolbar>
  );
}
```

- [ ] **Step 3: Update `BrowserPage` to hold the ref**

```typescript
export function BrowserPage({
  addressBarRef,
}: {
  addressBarRef: React.RefObject<HTMLInputElement | null>;
}) {
  const activeTabId = useStore(tabStore, (state) => state.activeTabId);

  return (
    <>
      {activeTabId ? <BrowserToolbar addressBarRef={addressBarRef} /> : null}
      {!activeTabId ? <EmptyState /> : null}
    </>
  );
}
```

- [ ] **Step 4: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: Type error in `layout.tsx` because `BrowserPage` now requires `addressBarRef` prop — will be fixed in Task 9.

- [ ] **Step 5: Commit**

```bash
git add apps/veil/src/renderer/app/browser/page.tsx apps/veil/src/renderer/app/browser/_components/toolbar.tsx
git commit -m "feat: add address bar ref and Escape-to-unfocus handling"
```

---

### Task 8: Create Tab Switcher Overlay Component

**Files:**
- Create: `apps/veil/src/renderer/components/tab-switcher.tsx`

- [ ] **Step 1: Create the MRU overlay component**

Create `apps/veil/src/renderer/components/tab-switcher.tsx`:

```typescript
import { cn } from "@pane/ui/cn";
import { useCallback, useEffect, useRef, useState } from "react";
import { useStore } from "zustand/react";

import {
  PROFILE_COLOR_HEX,
  type ProfileColor,
} from "../../constants/profile-colors";
import { profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import { trpc } from "../trpc";

const MAX_VISIBLE_TABS = 8;

interface MruTab {
  id: string;
  title: string;
  favicon: string;
  profileId: string;
  profileName: string;
  profileColor: ProfileColor;
}

function resolveMruTabs(mruHistory: string[]): MruTab[] {
  const profiles = profileStore.getState().profiles;
  const tabs: MruTab[] = [];

  for (const tabId of mruHistory) {
    if (tabs.length >= MAX_VISIBLE_TABS) {
      break;
    }

    for (const profile of profiles) {
      const tab = profile.tabs.find((tab) => tab.id === tabId);

      if (tab) {
        tabs.push({
          id: tab.id,
          title: tab.title || "Loading...",
          favicon: tab.favicon || "",
          profileId: profile.id,
          profileName: profile.name,
          profileColor: profile.color,
        });

        break;
      }
    }
  }

  return tabs;
}

export function TabSwitcher({
  visible,
  stepCounter,
  onConfirm,
  onCancel,
}: {
  visible: boolean;
  stepCounter: number;
  onConfirm: (tabId: string) => void;
  onCancel: () => void;
}) {
  const mruHistory = useStore(tabStore, (state) => state.mruHistory);
  const [selectedIndex, setSelectedIndex] = useState(1);
  const [tabs, setTabs] = useState<MruTab[]>([]);
  const lastStepRef = useRef(stepCounter);

  useEffect(() => {
    if (visible) {
      const resolved = resolveMruTabs(mruHistory);
      setTabs(resolved);
      setSelectedIndex(Math.min(1, resolved.length - 1));
      lastStepRef.current = stepCounter;
    }
  }, [visible, mruHistory, stepCounter]);

  useEffect(() => {
    if (!visible || tabs.length === 0 || stepCounter === lastStepRef.current) {
      return;
    }

    const delta = stepCounter - lastStepRef.current;
    lastStepRef.current = stepCounter;

    if (delta > 0) {
      setSelectedIndex((prev) => (prev + 1) % tabs.length);
    } else {
      setSelectedIndex((prev) => (prev - 1 + tabs.length) % tabs.length);
    }
  }, [stepCounter, visible, tabs.length]);

  const handleKeyUp = useCallback(
    (e: KeyboardEvent) => {
      if (e.key === "Control") {
        const tab = tabs[selectedIndex];

        if (tab) {
          onConfirm(tab.id);
        } else {
          onCancel();
        }
      }
    },
    [tabs, selectedIndex, onConfirm, onCancel],
  );

  const handleKeyDown = useCallback(
    (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        onCancel();
      }
    },
    [onCancel],
  );

  useEffect(() => {
    if (!visible) {
      return;
    }

    window.addEventListener("keyup", handleKeyUp);
    window.addEventListener("keydown", handleKeyDown);

    return () => {
      window.removeEventListener("keyup", handleKeyUp);
      window.removeEventListener("keydown", handleKeyDown);
    };
  }, [visible, handleKeyUp, handleKeyDown]);

  if (!visible || tabs.length === 0) {
    return null;
  }

  return (
    <div className="absolute inset-0 z-[60] flex items-center justify-center">
      <div className="absolute inset-0 bg-black/40" />

      <div className="relative w-[320px] rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl">
        {tabs.map((tab, index) => (
          <div
            key={tab.id}
            className={cn(
              "flex items-center gap-2.5 rounded-lg px-3 py-2 transition-colors",
              index === selectedIndex && "bg-white/10",
            )}
          >
            <div
              className="h-2 w-2 shrink-0 rounded-full"
              style={{
                backgroundColor: PROFILE_COLOR_HEX[tab.profileColor],
              }}
            />

            {tab.favicon ? (
              <img
                src={tab.favicon}
                alt=""
                className="h-4 w-4 shrink-0 rounded-sm"
              />
            ) : (
              <div className="h-4 w-4 shrink-0 rounded-sm bg-white/10" />
            )}

            <span className="truncate text-sm text-white/80">{tab.title}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: No type errors for this file in isolation

- [ ] **Step 3: Commit**

```bash
git add apps/veil/src/renderer/components/tab-switcher.tsx
git commit -m "feat: create Arc-style MRU tab switcher overlay component"
```

---

### Task 9: Wire Hotkey Subscription and Components into Layout

**Files:**
- Modify: `apps/veil/src/renderer/app/layout.tsx`

This is the final integration task. Layout subscribes to the hotkey tRPC subscription, manages the tab switcher overlay state, and passes the address bar ref to `BrowserPage`.

- [ ] **Step 1: Add hotkey subscription hook**

Create a custom hook file at `apps/veil/src/renderer/hooks/use-hotkey-events.ts`:

```typescript
import { useEffect, useRef } from "react";

import { trpc } from "../trpc";

export { HotkeyEvent } from "../../main/hotkey-emitter";
import { HotkeyEvent } from "../../main/hotkey-emitter";

export function useHotkeyEvents(
  handler: (event: HotkeyEvent) => void,
): void {
  const handlerRef = useRef(handler);
  handlerRef.current = handler;

  useEffect(() => {
    const subscription = trpc.hotkeys.events.subscribe(undefined, {
      onData(event: string) {
        handlerRef.current(event as HotkeyEvent);
      },
      onError(error) {
        console.error("[hotkeys] subscription error:", error);
      },
    });

    return () => subscription.unsubscribe();
  }, []);
}
```

- [ ] **Step 2: Update Layout to use the hook and render the overlay**

In `apps/veil/src/renderer/app/layout.tsx`, add imports and wire everything together.

Add to imports:

```typescript
import { TabSwitcher } from "../components/tab-switcher";
import { HotkeyEvent, useHotkeyEvents } from "../hooks/use-hotkey-events";
```

Inside the `Layout` component, after the existing state declarations and before the `return`, add:

```typescript
  const addressBarRef = useRef<HTMLInputElement>(null);

  const [tabSwitcherVisible, setTabSwitcherVisible] = useState(false);
  const [tabSwitcherStep, setTabSwitcherStep] = useState(0);

  useHotkeyEvents(
    useCallback(
      (event: HotkeyEvent) => {
        switch (event) {
          case HotkeyEvent.FOCUS_ADDRESS_BAR: {
            if (page === Page.BROWSER) {
              addressBarRef.current?.focus();
              addressBarRef.current?.select();
            }

            break;
          }

          case HotkeyEvent.TAB_SWITCHER_FORWARD: {
            setTabSwitcherVisible(true);
            setTabSwitcherStep((prev) => prev + 1);

            break;
          }

          case HotkeyEvent.TAB_SWITCHER_BACKWARD: {
            setTabSwitcherVisible(true);
            setTabSwitcherStep((prev) => prev - 1);

            break;
          }
        }
      },
      [page],
    ),
  );

  const handleTabSwitcherConfirm = useCallback((tabId: string) => {
    setTabSwitcherVisible(false);
    setTabSwitcherStep(0);

    navigationStore.getState().navigate(Page.BROWSER);
    trpc.tabs.switch.mutate({ tabId });
  }, []);

  const handleTabSwitcherCancel = useCallback(() => {
    setTabSwitcherVisible(false);
    setTabSwitcherStep(0);
  }, []);
```

- [ ] **Step 3: Update the JSX to pass addressBarRef and render TabSwitcher**

In the `return` statement, update `BrowserPage` to pass the ref, and add `TabSwitcher` before the pin screen overlay:

Replace:
```tsx
<ContentPanel>
  {page === Page.BROWSER ? <BrowserPage /> : null}
  {page === Page.SETTINGS ? <SettingsPage /> : null}
</ContentPanel>
```

With:
```tsx
<ContentPanel>
  {page === Page.BROWSER ? (
    <BrowserPage addressBarRef={addressBarRef} />
  ) : null}
  {page === Page.SETTINGS ? <SettingsPage /> : null}
</ContentPanel>

{tabSwitcherVisible && (
  <TabSwitcher
    visible={tabSwitcherVisible}
    stepCounter={tabSwitcherStep}
    onConfirm={handleTabSwitcherConfirm}
    onCancel={handleTabSwitcherCancel}
  />
)}
```

Also add `useRef` to the existing React imports if not already there (it is already imported).

- [ ] **Step 4: Verify build**

Run: `cd /Users/andrevictor/www/pane && turbo run typecheck --filter=@pane/veil`
Expected: No type errors

- [ ] **Step 5: Run lint**

Run: `cd /Users/andrevictor/www/pane && turbo run lint --filter=@pane/veil`
Expected: No lint errors (fix any class sorting or unused import issues)

- [ ] **Step 6: Commit**

```bash
git add apps/veil/src/renderer/hooks/use-hotkey-events.ts apps/veil/src/renderer/app/layout.tsx
git commit -m "feat: wire hotkey events subscription and tab switcher into layout"
```

---

### Task 10: Manual Testing

No code changes — verify all hotkeys work in the running app.

- [ ] **Step 1: Start dev server**

Run: `cd /Users/andrevictor/www/pane && turbo run dev --filter=@pane/veil`

- [ ] **Step 2: Test basic navigation hotkeys**

1. Open a profile and create a tab
2. Navigate to a website
3. Press `Cmd+R` — page should reload
4. Press `Cmd+[` — should go back
5. Press `Cmd+]` — should go forward

- [ ] **Step 3: Test tab management hotkeys**

1. Press `Cmd+T` — new tab should open in active profile
2. Press `Cmd+W` — active tab should close
3. Press `Cmd+Shift+T` — last closed tab should reopen with same URL in same profile
4. Press `Cmd+1` — should switch to first tab in active profile

- [ ] **Step 4: Test address bar hotkeys**

1. Press `Cmd+L` — address bar should focus and select all text
2. Press `Escape` while address bar focused — should unfocus and reset to current URL
3. Press `Cmd+L` while on Settings page — should be a no-op

- [ ] **Step 5: Test MRU tab switcher**

1. Open 3+ tabs across multiple profiles
2. Switch between tabs to build MRU history
3. Press `Ctrl+Tab` — overlay should appear with MRU list, 2nd item selected
4. While holding `Ctrl`, press `Tab` again — selection should cycle forward
5. Release `Ctrl` — overlay should close and switch to selected tab
6. Press `Ctrl+Tab` then `Escape` — overlay should close, stay on current tab
7. Press `Ctrl+Shift+Tab` — should cycle backward through MRU

- [ ] **Step 6: Test edge cases**

1. `Cmd+T` with no active profile — should be a no-op
2. `Cmd+W` with no active tab — should be a no-op
3. `Cmd+Shift+T` with no closed tabs — should be a no-op
4. `Cmd+9` when profile has fewer than 9 tabs — should be a no-op
5. `Cmd+R` while on Settings page — should be a no-op
6. `Ctrl+Tab` with only one tab — overlay should show one item
