# Pane Architecture Redesign — Domain-Driven OOP with ECWS

## Overview

Restructure the desktop app's main process from scattered singletons (`TabManager`, `ExtensionManager`, `CwsManager`) into a domain-driven class hierarchy: `Pane` > `Profile` > `ProfileTabs` / `ProfileExtensions`. Replace custom CWS code with `electron-chrome-web-store` (ECWS). Drop per-profile extension enable/disable for now.

## Problem

The current architecture has five singletons maintaining their own `profileId`-keyed state, coordinated through subscriptions and callbacks:

- `profileStore` — flat array of `BrowserProfile[]`
- `TabManager` — `Map<string, WebContentsView>` (tabId → view)
- `ExtensionManager` — `Map<string, ECE>` (profileId → ECE instance)
- `CwsManager` — `Set<string>` (loaded profiles) + CWS lifecycle
- `extensionStore` — `Record<string, ExtensionInfo[]>` (profileId → loaded extensions)

A "profile" is a cohesive domain concept — it has a session, tabs, extensions, and an identity — but its behavior is scattered across unrelated singletons. This is state-at-a-distance with hidden dependencies.

## Decisions

- **`Pane` is the top-level class.** It owns the profile collection and global extension operations. Named after the product. Matches the existing `window.pane` preload bridge.
- **`Profile` is a class with nested contexts.** `profile.tabs` for tab operations, `profile.extensions` for per-session extension operations. Each context is a lightweight class created in the Profile constructor.
- **Store stays source of truth for serializable data.** `profileStore` owns persistence + renderer sync. `Profile` class reads from it and writes to it for mutations. The class owns runtime-only state (session, ECE, WebContentsViews).
- **ECWS as npm dependency.** `electron-chrome-web-store` handles CWS download, CRX3 parsing, Omaha update protocol. Our custom code (cws-downloader, cws-updater, cws-manager, cws-store) is deleted.
- **Drop per-profile enable/disable.** All installed extensions load into every profile via `loadAllExtensions()`. The `enabledExtensions` field on `BrowserProfile` is removed.

## Class Hierarchy

```
Pane (top-level, one per process)
├── extensions: PaneExtensions
│   ├── install(extensionId) → download from CWS
│   ├── uninstall(extensionId) → remove from disk + all sessions
│   ├── installed() → scan dir
│   └── checkForUpdates() → Omaha protocol
├── createProfile(input) → Profile
├── getProfile(id) → Profile
├── removeProfile(id)
├── registerIpc()
└── restore()

Profile (per-profile, owns runtime state)
├── tabs: ProfileTabs
│   ├── open(url?, tabId?) → WebContentsView
│   ├── close(tabId)
│   ├── activate(tabId)
│   ├── closeAll()
│   └── resizeAll()
├── extensions: ProfileExtensions
│   ├── ensureLoaded() → loads all installed into session (lazy, once)
│   ├── unload(extensionId)
│   └── getLoaded() → Extension[]
├── id, session, data (reads from profileStore)
└── destroy()
```

## File Structure

### Create

```
apps/desktop/src/main/
├── pane.ts                         # Pane class
├── pane-extensions.ts              # PaneExtensions (global CWS via ECWS)
├── profile.ts                      # Profile class
├── profile-tabs.ts                 # ProfileTabs
├── profile-extensions.ts           # ProfileExtensions
```

### Delete

```
apps/desktop/src/main/extensions/
├── extension-manager.ts            # Absorbed into Profile
├── cws-downloader.ts               # Replaced by ECWS
├── cws-updater.ts                  # Replaced by ECWS
├── cws-manager.ts                  # Absorbed into Pane + PaneExtensions

apps/desktop/src/main/browser/
├── tab-manager.ts                  # Absorbed into ProfileTabs

apps/desktop/src/stores/
├── cws-store.ts                    # Directory is source of truth (ECWS)
```

### Modify

```
apps/desktop/src/main/index.ts                  # Simplify to: new Pane(), registerIpc(), restore()
apps/desktop/src/stores/profile-store.ts         # Remove enabledExtensions field + setEnabledExtensions action
apps/desktop/src/preload/index.ts                # Update IPC channels if needed
apps/desktop/package.json                        # Add electron-chrome-web-store, remove unzip-crx-3
```

### Keep (unchanged)

```
apps/desktop/src/stores/extension-store.ts       # Renderer queries loaded extensions
apps/desktop/src/stores/settings-store.ts
apps/desktop/src/stores/tab-store.ts
apps/desktop/src/stores/navigation-store.ts
apps/desktop/src/stores/middlewares/
apps/desktop/src/main/store-sync.ts              # Remove cws-store entry in index.ts
apps/desktop/src/main/browser/detect-browser.ts
packages/electron-chrome-extensions/             # No changes
```

## Dependency Changes

| Action | Package | Reason |
|--------|---------|--------|
| Add | `electron-chrome-web-store` | CWS download, CRX parsing, Omaha updates |
| Remove | `unzip-crx-3` | Replaced by ECWS (uses `adm-zip` internally) |

## API Detail

### `Pane`

```ts
class Pane {
  readonly extensions: PaneExtensions;
  private readonly profiles: Map<string, Profile>;
  private readonly extensionsPath: string;

  constructor(mainWindow: BaseWindow);

  createProfile(input: CreateInput): Profile;
  getProfile(id: string): Profile | undefined;
  removeProfile(id: string): void;
  allProfiles(): Profile[];

  registerIpc(): void;
  restore(): void;
  resizeAllTabs(): void;
}
```

### `PaneExtensions`

```ts
class PaneExtensions {
  constructor(pane: Pane, extensionsPath: string);

  install(extensionId: string): Promise<Extension | null>;
  uninstall(extensionId: string): Promise<void>;
  installed(): InstalledExtension[];
  checkForUpdates(): Promise<void>;
}
```

`install()` calls ECWS's `installExtension()`, then iterates all active profiles calling `profile.extensions.ensureLoaded()`.

`uninstall()` iterates all active profiles calling `profile.extensions.unload(id)`, then calls ECWS's `uninstallExtension()`.

`installed()` scans `extensionsPath` for manifest.json files (or uses ECWS's discovery).

`checkForUpdates()` calls ECWS's `updateExtensions()`. Fire-and-forget at startup.

### `Profile`

```ts
class Profile {
  readonly id: string;
  readonly session: Electron.Session;
  readonly tabs: ProfileTabs;
  readonly extensions: ProfileExtensions;

  constructor(id: string, mainWindow: BaseWindow, extensionsPath: string);

  get data(): BrowserProfile;  // reads from profileStore
  destroy(): void;
}
```

Constructor creates the Electron session (`persist:profile-<id>`), registers the CRX protocol, and creates the ECE instance with `createTab`/`selectTab`/`removeTab` callbacks wired to `this.tabs`.

`destroy()` calls `this.tabs.closeAll()` and `this.ece.destroy()`.

### `ProfileTabs`

```ts
class ProfileTabs {
  constructor(profile: Profile, mainWindow: BaseWindow);

  open(url?: string, tabId?: string): WebContentsView;
  close(tabId: string): void;
  activate(tabId: string): void;
  closeAll(): void;
  resizeAll(): void;
}
```

`open()` creates a `WebContentsView`, adds it to the window, loads the URL, calls `profile.extensions.ensureLoaded()` (lazy, first tab only), registers the tab with ECE via `ece.addTab()`, and updates `profileStore`.

`close()` removes from ECE, removes from window, closes WebContents, updates `profileStore`.

### `ProfileExtensions`

```ts
class ProfileExtensions {
  constructor(profile: Profile, extensionsPath: string);

  ensureLoaded(): Promise<void>;           // idempotent, loads all installed
  unload(extensionId: string): void;
  getLoaded(): Extension[];
}
```

`ensureLoaded()` calls ECWS's `loadAllExtensions(session, extensionsPath)` once per profile lifetime. Subsequent calls are no-ops.

## Startup Flow

```ts
// index.ts
app.whenReady().then(() => {
  profileStore.persist.rehydrate();
  settingsStore.persist.rehydrate();

  // ... settings detection, menu setup ...

  createWindow();    // creates mainWindow
  pane.registerIpc();
  pane.restore();    // restores profiles + tabs, fires update check
});
```

`pane.restore()` internally:
1. Iterates `profileStore.getState().profiles`
2. Creates a `Profile` instance for each
3. Opens persisted tabs via `profile.tabs.open(tab.url, tab.id)`
4. Calls `pane.extensions.checkForUpdates()` (fire-and-forget)

## IPC Surface

Registered in `Pane.registerIpc()`:

| Channel | Handler |
|---|---|
| `profiles:create` | `pane.createProfile(input)` |
| `profiles:remove` | `pane.removeProfile(id)` |
| `tabs:open` | `pane.getProfile(profileId)?.tabs.open(url)` |
| `tabs:close` | Find owning profile, `profile.tabs.close(tabId)` |
| `tabs:switch` | Find owning profile, `profile.tabs.activate(tabId)` |
| `tabs:navigate` | Load URL in active tab |
| `tabs:go-back` | Active tab goBack |
| `tabs:go-forward` | Active tab goForward |
| `tabs:reload` | Active tab reload |
| `tabs:hide-all` | Hide all tab views |
| `tabs:show-active` | Show active tab view |
| `cws:install` | `pane.extensions.install(extensionId)` |
| `cws:uninstall` | `pane.extensions.uninstall(extensionId)` |
| `cws:installed` | `pane.extensions.installed()` |
| `extensions:list` | `pane.getProfile(profileId)?.extensions.getLoaded()` |
| `settings:detect-browser` | Detect browser path |

## What This Does NOT Include

- **Per-profile extension enable/disable** — Dropped. All extensions load into every profile. Can be added later as a filter in `ProfileExtensions.ensureLoaded()`.
- **Extension management UI** — No renderer changes beyond IPC surface updates.
- **ECWS CWS page preload** — ECWS can register a preload that makes the Chrome Web Store page work as an install UI. Not wired up in this spec, but available for future use.
- **Changes to the ECE fork** — `packages/electron-chrome-extensions/` is untouched.
