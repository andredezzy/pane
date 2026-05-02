# ECWS Integration — Replace Custom CWS Code with electron-chrome-web-store

## Overview

Replace the custom CWS download/update implementation (cws-downloader, cws-updater, cws-manager, cws-store) with samuelmaddock's `electron-chrome-web-store` (ECWS) package. ECWS is a battle-tested, MIT-licensed package that handles CWS downloading, CRX3 parsing, Omaha-protocol update checking, and extension lifecycle management. It uses the filesystem as source of truth (no separate registry).

## Decisions

- **Use ECWS as an npm dependency, not a fork.** The package is maintained, MIT-licensed, and the only multi-session incompatibility (shared `lastUpdateCheck` singleton) is worked around by disabling `autoUpdate` and calling `updateExtensions()` manually once at startup.
- **Drop per-profile enable/disable.** All installed extensions load into every profile session via `loadAllExtensions()`. The `enabledExtensions` field on `BrowserProfile` is removed. Per-profile filtering can be added later.
- **Shared extensions path.** All profile sessions share `app.getPath("userData") + "/Extensions"`. ECWS downloads once, each session loads from the same directory.
- **Keep ECE and ECWS separate.** They're independent packages that both talk to `session.extensions`. This matches the upstream architecture.

## What ECWS Provides (that our code doesn't)

- Proper CRX3 protobuf header parsing (our code used `unzip-crx-3` which is simpler)
- Writes `manifest.key` into the unpacked manifest (critical for stable extension IDs across updates)
- Omaha v3.1 protocol for update checks (our code re-downloaded the entire .crx to check versions)
- Extension ID derivation from public key hash
- Preload that makes the actual Chrome Web Store page work as an install UI
- Allowlist/denylist support
- `beforeInstall` hook for install confirmation

## File Changes

### Delete (our custom CWS code)

- `apps/desktop/src/main/extensions/cws-downloader.ts`
- `apps/desktop/src/main/extensions/cws-updater.ts`
- `apps/desktop/src/main/extensions/cws-manager.ts`
- `apps/desktop/src/stores/cws-store.ts`

### Remove dependency

- `unzip-crx-3` from `apps/desktop/package.json`

### Add dependency

- `electron-chrome-web-store` to `apps/desktop/package.json`

### Modify

- `apps/desktop/src/main/extensions/extension-manager.ts` — Absorb CWS lifecycle: IPC handlers, extension loading via ECWS, update check at startup.
- `apps/desktop/src/main/index.ts` — Remove `cwsStore` import/rehydration/StoreSync registration, remove `CwsManager` creation/wiring, simplify to just `ExtensionManager`.
- `apps/desktop/src/main/store-sync.ts` — No code change, but `cws-store` entry removed from the StoreSync constructor in `index.ts`.
- `apps/desktop/src/stores/profile-store.ts` — Remove `enabledExtensions` field from `BrowserProfile`, `CreateInput`, `create` action, `merge` handler, and `setEnabledExtensions` action.
- `apps/desktop/src/preload/index.ts` — Update `cws` namespace to match the new IPC surface.

## New ExtensionManager Design

`ExtensionManager` becomes the single orchestrator. It manages per-profile ECE instances (existing responsibility) AND delegates CWS operations to ECWS (absorbing what `CwsManager` did).

```ts
import { installExtension, uninstallExtension, loadAllExtensions, updateExtensions } from 'electron-chrome-web-store';

class ExtensionManager {
  // Existing: per-profile ECE instance management
  private readonly instances = new Map<string, ElectronChromeExtensions>();
  private readonly loadedProfiles = new Set<string>();

  // Shared extensions path
  private readonly extensionsPath: string;

  constructor(tabManager, mainWindow) {
    this.extensionsPath = path.join(app.getPath("userData"), "Extensions");
  }

  // NEW: Load all extensions into a profile's session
  async loadExtensionsForProfile(profileId: string): Promise<void> {
    if (this.loadedProfiles.has(profileId)) return;
    this.loadedProfiles.add(profileId);
    this.getOrCreateInstance(profileId);
    const ses = session.fromPartition(`persist:profile-${profileId}`);
    await loadAllExtensions(ses, this.extensionsPath);
  }

  // NEW: Install from CWS (delegates to ECWS)
  async installFromCWS(extensionId: string): Promise<Extension | null> {
    // installExtension downloads + unpacks + loads into session
    // We just need to install to disk; loading happens per-profile
    // Use downloadExtension or installExtension with a temp session
  }

  // NEW: Uninstall (delegates to ECWS)
  async uninstallFromCWS(extensionId: string): Promise<void> {
    // Remove from all active sessions + delete from disk
  }

  // NEW: Check for updates (once at startup)
  async checkForUpdates(): Promise<void> {
    // Call updateExtensions with one active session
  }

  // NEW: IPC registration (absorbs CwsManager's IPC)
  registerIpc() {
    // extensions:list, extensions:load (existing)
    // cws:install, cws:uninstall, cws:installed (new, delegates to ECWS)
  }

  // Existing: registerTab, activateTab, unregisterTab, destroyProfile
  // Existing: private getOrCreateInstance
}
```

## IPC Surface

| Channel | Args | Returns | Purpose |
|---|---|---|---|
| `cws:install` | `extensionId: string` | `{ id, name, version } \| null` | Download + install from CWS |
| `cws:uninstall` | `extensionId: string` | `void` | Remove extension + files |
| `cws:installed` | none | `{ id, name, version }[]` | List installed extensions (scan dir) |
| `extensions:list` | `profileId: string` | `ExtensionInfo[]` | List loaded extensions for a profile (existing) |
| `extensions:load` | `profileId, extPath` | `ExtensionInfo \| null` | Load extension into profile (existing) |

## Startup Flow

```
1. profileStore.persist.rehydrate()
2. settingsStore.persist.rehydrate()
3. (cwsStore rehydration REMOVED)
4. createWindow() — creates ExtensionManager
5. extensionManager.registerIpc()
6. extensionManager.checkForUpdates() — async, fire-and-forget
7. On first tab per profile: extensionManager.loadExtensionsForProfile(profileId)
```

## What This Does NOT Change

- ECE fork (`packages/electron-chrome-extensions/`) — no changes
- `extensionStore` — stays for renderer to query loaded extensions per session
- The ECE instance-per-profile model — unchanged
- Tab management callbacks (onTabCreated, etc.) — unchanged

## Open Questions for Implementation

- **ECWS's `installExtension()` takes a session and loads the extension into it.** For Pane, we want to install to disk without loading (since we load per-profile via `loadAllExtensions`). Need to check if `downloadExtension()` (the lower-level function) is exported, or if we need to call `installExtension` with a throwaway session.
- **ECWS's `uninstallExtension()` calls `session.extensions.removeExtension()`.** We need to call it for every active session that has the extension loaded, not just one.
- **The `lastUpdateCheck` singleton** — mitigated by calling `updateExtensions()` once with any active session. Updates download to the shared path, so all sessions benefit on next load.
