# CWS Download & Auto-Update

## Overview

Replace the temporary hardcoded NordPass loading hack with a proper Chrome Web Store integration. Extensions are installed globally (downloaded once, shared on disk) and enabled per-profile.

## Decisions

- **Global install + per-profile enable/disable.** Extensions download once to a managed directory. Each profile has an `enabledExtensions: string[]` field controlling which are loaded.
- **Update check on app launch only.** No timers or background polling. A single async check at startup, non-blocking.
- **CRX unpacking via `unzip-crx-3`.** The de facto Electron ecosystem standard (181k weekly downloads, used by `electron-devtools-installer`). Handles both CRX2 and CRX3.
- **Separate stores for installs vs. assignments.** A new persisted `cwsStore` owns what's on disk. `BrowserProfile.enabledExtensions` owns what each profile uses. The existing in-memory `extensionStore` continues to track what's loaded in the current Electron session.

## Data Model

### New `cwsStore` (persisted to disk via `fsStorage`)

```ts
interface CwsExtension {
  id: string;           // CWS extension ID (e.g., "eiaeiblijfjekdanodkjadfinkhbfgcd")
  name: string;         // From manifest.json after unpacking
  version: string;      // From manifest.json
  path: string;         // Absolute path to unpacked directory
  installedAt: string;  // ISO timestamp
  updatedAt: string;    // ISO timestamp
}

interface CwsState {
  extensions: CwsExtension[];
  install: (ext: CwsExtension) => void;
  update: (id: string, partial: Partial<CwsExtension>) => void;
  uninstall: (id: string) => void;
}
```

### Extended `BrowserProfile`

```ts
interface BrowserProfile {
  // ... existing fields ...
  enabledExtensions: string[];  // Array of CWS extension IDs
}
```

New profiles get all currently installed extension IDs by default. The `enabledExtensions` field is persisted with the profile (not stripped by `partialize`).

## File Structure

```
apps/desktop/src/main/extensions/
├── extension-manager.ts          # Existing — unchanged
├── cws-downloader.ts             # Download .crx from CWS, unpack to managed dir
├── cws-updater.ts                # Check for updates on app launch
└── cws-manager.ts                # Orchestrator: ties downloader, updater, stores, IPC

apps/desktop/src/stores/
├── cws-store.ts                  # New persisted store
├── profile-store.ts              # Extended with enabledExtensions
```

**Managed directory:** `~/Library/Application Support/@pane/desktop/Extensions/<extensionId>/<version>/`

## Module Responsibilities

### `cws-downloader.ts`

Pure function. Given an extension ID:

1. Fetch from `https://clients2.google.com/service/update2/crx?response=redirect&prodversion=<CHROME_VERSION>&acceptformat=crx2,crx3&x=id%3D<EXTENSION_ID>%26uc` using native `fetch()` (follows redirects automatically).
2. Write `.crx` to a temp file in the managed directory.
3. Call `unzip-crx-3` to extract to a temp directory.
4. Read `manifest.json` from the extracted files to get `name` and `version`.
5. Rename temp dir to `Extensions/<extensionId>/<version>/`.
6. Clean up the `.crx` temp file.
7. Return `CwsExtension` with path, name, version, timestamps.

Uses `process.versions.chrome` for the `prodversion` parameter.

No state, no store access. Caller handles store updates.

### `cws-updater.ts`

Pure function. Given a list of installed `CwsExtension[]`:

For each extension, re-downloads the .crx from CWS and reads the manifest version. If the version is newer than the currently installed version, keeps the new extraction and returns the updated `CwsExtension`. If the version is the same, deletes the temp extraction.

This avoids the complex Google update protocol. The trade-off is downloading the full .crx even when there's no update, but this only runs once on app launch.

Returns a list of updated extensions (empty if everything is current).

### `cws-manager.ts`

Orchestrator class. Constructor takes `ExtensionManager`.

Public methods:

- **`install(extensionId: string): Promise<CwsExtension | null>`** — Calls downloader, writes to `cwsStore`, adds the extension ID to all existing profiles' `enabledExtensions`.
- **`uninstall(extensionId: string): Promise<void>`** — Removes from `cwsStore`, removes from all profiles' `enabledExtensions`, deletes files from disk.
- **`checkForUpdates(): Promise<void>`** — Reads installed extensions from `cwsStore`, calls updater, updates `cwsStore` entries with new version/path/updatedAt for any that changed. Async, non-blocking, called at startup.
- **`loadExtensionsForProfile(profileId: string): Promise<void>`** — Reads `profile.enabledExtensions`, looks up each path in `cwsStore`, calls `ExtensionManager.loadExtension(profileId, path)` for each.
- **`toggleExtension(profileId: string, extensionId: string): void`** — Adds or removes the extension ID from the profile's `enabledExtensions`.
- **`registerIpc(): void`** — Registers the IPC handlers below.

## IPC Surface

| Channel | Args | Returns | Purpose |
|---|---|---|---|
| `cws:install` | `extensionId: string` | `CwsExtension \| null` | Download + install from CWS |
| `cws:uninstall` | `extensionId: string` | `void` | Remove extension + files |
| `cws:installed` | none | `CwsExtension[]` | List all installed extensions |
| `cws:toggle` | `profileId: string, extensionId: string` | `void` | Enable/disable for a profile |

## Startup Flow

In `index.ts`, replacing the temporary auto-load hack:

1. Rehydrate `cwsStore` (persisted).
2. Rehydrate `profileStore` (persisted).
3. Create `CwsManager` (takes `ExtensionManager` reference).
4. `CwsManager.registerIpc()`.
5. `CwsManager.checkForUpdates()` — async, non-blocking (fire and forget with error logging).
6. Track which profiles have had extensions loaded (a `Set<string>` inside `CwsManager`). In `index.ts`, hook into the existing `tabManager.onTabCreated` callback — before calling `extensionManager.registerTab(wc, profileId)`, call `cwsManager.loadExtensionsForProfile(profileId)`. `loadExtensionsForProfile` checks the set and no-ops if already loaded for that profile.

The profile deletion cleanup subscription stays as-is — `ExtensionManager.destroyProfile(profileId)` already handles it. `CwsManager` should also clear the profile from its loaded set on deletion.

### New profile creation

`CwsManager` subscribes to `profileStore` and detects newly added profiles. When a new profile appears, it sets `enabledExtensions` to all currently installed extension IDs from `cwsStore`. This keeps the store coupling inside the orchestrator rather than making `profileStore.create()` depend on `cwsStore`.

## Preload Bridge

Extend the existing `window.pane` bridge in `preload/index.ts`:

```ts
cws: {
  install: (extensionId: string) => ipcRenderer.invoke("cws:install", extensionId),
  uninstall: (extensionId: string) => ipcRenderer.invoke("cws:uninstall", extensionId),
  installed: () => ipcRenderer.invoke("cws:installed"),
  toggle: (profileId: string, extensionId: string) => ipcRenderer.invoke("cws:toggle", profileId, extensionId),
}
```

## What Changes in Existing Files

- **`index.ts`** — Remove the temp auto-load hack (lines 147-163). Add `cwsStore` rehydration, `CwsManager` creation, IPC registration, and profile extension loading subscription.
- **`profile-store.ts`** — Add `enabledExtensions: string[]` to `BrowserProfile`. Default to `[]` in `create()`. Add to `merge` handler so existing persisted profiles get `enabledExtensions: []` on first load.
- **`preload/index.ts`** — Add `cws` namespace to the `window.pane` bridge.
- **`extension-manager.ts`** — No changes.

## What This Does NOT Include

- **Extension management UI** — No renderer components. The IPC surface is ready for a future UI.
- **Content script injection fixes** — Out of scope, separate concern.
- **Context menu wiring** — Out of scope.
- **Old extension version cleanup** — When an extension updates, the old version directory is deleted and replaced by the new one. No multi-version coexistence.
