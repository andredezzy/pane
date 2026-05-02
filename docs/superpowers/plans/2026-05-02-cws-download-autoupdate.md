# CWS Download & Auto-Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the temporary hardcoded NordPass loading hack with a proper Chrome Web Store download, install, and launch-time update system.

**Architecture:** Extensions are installed globally to a managed directory (`~/Library/Application Support/@pane/desktop/Extensions/<id>/<version>/`), tracked in a persisted `cwsStore`, and enabled per-profile via a new `enabledExtensions` field on `BrowserProfile`. A `CwsManager` orchestrator ties together a stateless downloader, an updater, the stores, and IPC handlers.

**Tech Stack:** Electron 41.4.0, Zustand 5 (vanilla stores with persist + sync middlewares), native `fetch()`, `unzip-crx-3` for CRX unpacking, `node:fs` / `node:path` for file operations.

**Spec:** `docs/superpowers/specs/2026-05-02-cws-download-autoupdate-design.md`

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `apps/desktop/src/stores/cws-store.ts` | Persisted store tracking globally installed CWS extensions |
| Create | `apps/desktop/src/main/extensions/cws-downloader.ts` | Pure function: download .crx from CWS, unpack, return metadata |
| Create | `apps/desktop/src/main/extensions/cws-updater.ts` | Pure function: check for updates by re-downloading and comparing versions |
| Create | `apps/desktop/src/main/extensions/cws-manager.ts` | Orchestrator: install/uninstall/update lifecycle, IPC, profile subscriptions |
| Modify | `apps/desktop/src/stores/profile-store.ts` | Add `enabledExtensions: string[]` to `BrowserProfile` |
| Modify | `apps/desktop/src/preload/index.ts` | Add `cws` namespace to `window.pane` bridge |
| Modify | `apps/desktop/src/main/index.ts` | Wire CwsManager, remove temp hack, add profile extension loading |
| Modify | `apps/desktop/src/main/store-sync.ts` | Register `cws-store` (done in `index.ts` via StoreSync constructor) |

---

### Task 1: Install `unzip-crx-3` dependency

**Files:**
- Modify: `apps/desktop/package.json`

- [ ] **Step 1: Install the package**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && bun add unzip-crx-3
```

- [ ] **Step 2: Verify it installed**

```bash
cd /Users/andrevictor/www/pane && cat apps/desktop/package.json | grep unzip-crx
```

Expected: `"unzip-crx-3": "^0.2.0"` (or similar) in dependencies.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/package.json bun.lock
git commit -m "feat(desktop): add unzip-crx-3 for CRX unpacking"
```

---

### Task 2: Create `cwsStore`

**Files:**
- Create: `apps/desktop/src/stores/cws-store.ts`

- [ ] **Step 1: Create the store file**

Create `apps/desktop/src/stores/cws-store.ts`:

```ts
import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";

import { fsStorage } from "./middlewares/fs-storage";
import { sync } from "./middlewares/sync";

export interface CwsExtension {
	id: string;
	name: string;
	version: string;
	path: string;
	installedAt: string;
	updatedAt: string;
}

interface CwsState {
	extensions: CwsExtension[];

	install: (ext: CwsExtension) => void;
	update: (id: string, partial: Partial<CwsExtension>) => void;
	uninstall: (id: string) => void;
}

export const cwsStore = createStore<CwsState>()(
	persist(
		sync(
			(set) => ({
				extensions: [],

				install: (ext) =>
					set((s) => ({ extensions: [...s.extensions, ext] })),

				update: (id, partial) =>
					set((s) => ({
						extensions: s.extensions.map((e) =>
							e.id === id ? { ...e, ...partial } : e,
						),
					})),

				uninstall: (id) =>
					set((s) => ({
						extensions: s.extensions.filter((e) => e.id !== id),
					})),
			}),
			{ name: "cws-store" },
		),
		{
			name: "cws-extensions",
			storage: createJSONStorage(() => fsStorage),
			skipHydration: true,
			partialize: (state) => ({ extensions: state.extensions }),
		},
	),
);
```

- [ ] **Step 2: Verify the file compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit src/stores/cws-store.ts 2>&1 | head -20
```

If `tsc` complains about imports (since it's part of a larger build), alternatively verify by running the full build:

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/cws-store.ts
git commit -m "feat(desktop): add persisted cwsStore for installed CWS extensions"
```

---

### Task 3: Extend `BrowserProfile` with `enabledExtensions`

**Files:**
- Modify: `apps/desktop/src/stores/profile-store.ts:59-88` (interface + create + merge)

- [ ] **Step 1: Add `enabledExtensions` to `BrowserProfile` interface**

In `apps/desktop/src/stores/profile-store.ts`, add the field to the `BrowserProfile` interface (after line 70, before `isExpanded`):

```ts
export interface BrowserProfile {
	id: string;
	name: string;
	color: ProfileColor;
	group: string | null;
	notes: string | null;
	fingerprint: Fingerprint;
	proxy: ProxyConfig | null;
	tags: string[];
	tabs: Tab[];
	enabledExtensions: string[];
	isExpanded: boolean;
	createdAt: string;
	updatedAt: string;
}
```

- [ ] **Step 2: Add `enabledExtensions` to `CreateInput` exclusion and `create` action**

The `CreateInput` type already excludes `id`, `createdAt`, `updatedAt`, `tabs`, `isExpanded`. Add `enabledExtensions` to the exclusion so callers don't need to provide it:

```ts
type CreateInput = Omit<
	BrowserProfile,
	"id" | "createdAt" | "updatedAt" | "tabs" | "isExpanded" | "enabledExtensions"
>;
```

In the `create` action, add `enabledExtensions: []` to the new profile object (after `tabs: []`):

```ts
create: (input) => {
	const now = new Date().toISOString();

	set((s) => ({
		profiles: [
			...s.profiles,
			{
				...input,
				id: crypto.randomUUID(),
				tabs: [],
				enabledExtensions: [],
				isExpanded: false,
				createdAt: now,
				updatedAt: now,
			},
		],
	}));
},
```

- [ ] **Step 3: Add `enabledExtensions` to the `merge` handler**

In the `merge` function (inside the `persist` config), add a fallback for `enabledExtensions` so existing persisted profiles without the field get `[]`:

```ts
merge: (persisted, current) => ({
	...current,
	...(persisted as Partial<ProfileState>),
	profiles: ((persisted as Partial<ProfileState>)?.profiles ?? []).map(
		(p) => ({
			...p,
			color: p.color ?? ProfileColor.BLUE,
			enabledExtensions: p.enabledExtensions ?? [],
			tabs: p.tabs.map((t) => ({ ...t, favicon: t.favicon ?? "" })),
			isExpanded: p.tabs.length > 0,
		}),
	),
}),
```

- [ ] **Step 4: Add `setEnabledExtensions` action to `ProfileState`**

Add a new action to update a profile's enabled extensions. This is needed by `CwsManager` to toggle extensions per-profile.

Add to the `ProfileState` interface:

```ts
interface ProfileState {
	profiles: BrowserProfile[];

	create: (input: CreateInput) => void;
	remove: (id: string) => void;
	toggleExpanded: (id: string) => void;
	openTab: (profileId: string, tabId: string, url: string) => void;
	closeTab: (tabId: string) => void;
	updateTab: (tabId: string, partial: Partial<Tab>) => void;
	setEnabledExtensions: (profileId: string, extensionIds: string[]) => void;
}
```

Add the implementation inside the store creator:

```ts
setEnabledExtensions: (profileId, extensionIds) => {
	set((s) => ({
		profiles: s.profiles.map((p) =>
			p.id === profileId
				? { ...p, enabledExtensions: extensionIds }
				: p,
		),
	}));
},
```

- [ ] **Step 5: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 6: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts
git commit -m "feat(desktop): add enabledExtensions to BrowserProfile"
```

---

### Task 4: Create `cws-downloader.ts`

**Files:**
- Create: `apps/desktop/src/main/extensions/cws-downloader.ts`

- [ ] **Step 1: Create the downloader module**

Create `apps/desktop/src/main/extensions/cws-downloader.ts`:

```ts
import fs from "node:fs";
import path from "node:path";
import { app } from "electron";
import unzip from "unzip-crx-3";

import type { CwsExtension } from "../../stores/cws-store";

const EXTENSIONS_DIR = () =>
	path.join(app.getPath("userData"), "Extensions");

function cwsDownloadUrl(extensionId: string): string {
	const chromeVersion = process.versions.chrome;
	return (
		"https://clients2.google.com/service/update2/crx" +
		`?response=redirect&prodversion=${chromeVersion}` +
		"&acceptformat=crx2,crx3" +
		`&x=id%3D${extensionId}%26uc`
	);
}

export async function downloadExtension(
	extensionId: string,
): Promise<CwsExtension> {
	const baseDir = EXTENSIONS_DIR();
	fs.mkdirSync(baseDir, { recursive: true });

	const tempCrx = path.join(baseDir, `${extensionId}.crx`);
	const tempDir = path.join(baseDir, `${extensionId}_tmp`);

	try {
		const response = await fetch(cwsDownloadUrl(extensionId));
		if (!response.ok) {
			throw new Error(`CWS returned ${response.status} for ${extensionId}`);
		}

		const buffer = Buffer.from(await response.arrayBuffer());
		fs.writeFileSync(tempCrx, buffer);

		if (fs.existsSync(tempDir)) {
			fs.rmSync(tempDir, { recursive: true });
		}

		await unzip(tempCrx, tempDir);

		const manifest = JSON.parse(
			fs.readFileSync(path.join(tempDir, "manifest.json"), "utf-8"),
		);
		const version: string = manifest.version;
		const name: string = manifest.name;

		const finalDir = path.join(baseDir, extensionId, version);
		if (fs.existsSync(finalDir)) {
			fs.rmSync(finalDir, { recursive: true });
		}
		fs.mkdirSync(path.dirname(finalDir), { recursive: true });
		fs.renameSync(tempDir, finalDir);

		const now = new Date().toISOString();
		return {
			id: extensionId,
			name,
			version,
			path: finalDir,
			installedAt: now,
			updatedAt: now,
		};
	} finally {
		if (fs.existsSync(tempCrx)) fs.unlinkSync(tempCrx);
		if (fs.existsSync(tempDir)) fs.rmSync(tempDir, { recursive: true });
	}
}
```

- [ ] **Step 2: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors. Note: `unzip-crx-3` may not have TypeScript types. If the import fails, add a declaration file.

- [ ] **Step 3: If `unzip-crx-3` has no types, create a declaration**

If the build fails with a missing module type error, create `apps/desktop/src/main/extensions/unzip-crx-3.d.ts`:

```ts
declare module "unzip-crx-3" {
	function unzip(crxPath: string, destination: string): Promise<void>;
	export = unzip;
}
```

Then rebuild and verify.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/extensions/cws-downloader.ts
# Also add the .d.ts if created:
git add apps/desktop/src/main/extensions/unzip-crx-3.d.ts 2>/dev/null
git commit -m "feat(desktop): add CWS downloader for .crx download and unpack"
```

---

### Task 5: Create `cws-updater.ts`

**Files:**
- Create: `apps/desktop/src/main/extensions/cws-updater.ts`

- [ ] **Step 1: Create the updater module**

Create `apps/desktop/src/main/extensions/cws-updater.ts`:

```ts
import fs from "node:fs";

import type { CwsExtension } from "../../stores/cws-store";
import { downloadExtension } from "./cws-downloader";

function isNewer(remote: string, local: string): boolean {
	const r = remote.split(".").map(Number);
	const l = local.split(".").map(Number);
	for (let i = 0; i < Math.max(r.length, l.length); i++) {
		const rv = r[i] ?? 0;
		const lv = l[i] ?? 0;
		if (rv > lv) return true;
		if (rv < lv) return false;
	}
	return false;
}

export async function checkForUpdates(
	installed: CwsExtension[],
): Promise<CwsExtension[]> {
	const updated: CwsExtension[] = [];

	for (const ext of installed) {
		try {
			const fresh = await downloadExtension(ext.id);

			if (isNewer(fresh.version, ext.version)) {
				fresh.installedAt = ext.installedAt;
				// Delete the old version directory
				if (fs.existsSync(ext.path)) {
					fs.rmSync(ext.path, { recursive: true });
				}
				updated.push(fresh);
			} else {
				// Same or older version — remove the fresh download
				if (fs.existsSync(fresh.path)) {
					fs.rmSync(fresh.path, { recursive: true });
				}
			}
		} catch (err) {
			console.error(`[CWS] Failed to check update for ${ext.id}:`, err);
		}
	}

	return updated;
}
```

- [ ] **Step 2: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extensions/cws-updater.ts
git commit -m "feat(desktop): add CWS updater for launch-time update checks"
```

---

### Task 6: Create `cws-manager.ts`

**Files:**
- Create: `apps/desktop/src/main/extensions/cws-manager.ts`

- [ ] **Step 1: Create the orchestrator**

Create `apps/desktop/src/main/extensions/cws-manager.ts`:

```ts
import fs from "node:fs";
import { ipcMain } from "electron";

import { cwsStore, type CwsExtension } from "../../stores/cws-store";
import { profileStore } from "../../stores/profile-store";
import type { ExtensionManager } from "./extension-manager";
import { downloadExtension } from "./cws-downloader";
import { checkForUpdates } from "./cws-updater";

export class CwsManager {
	private readonly loadedProfiles = new Set<string>();

	constructor(private readonly extensionManager: ExtensionManager) {
		this.subscribeToNewProfiles();
		this.subscribeToRemovedProfiles();
	}

	async install(extensionId: string): Promise<CwsExtension | null> {
		try {
			const ext = await downloadExtension(extensionId);
			cwsStore.getState().install(ext);

			const profiles = profileStore.getState().profiles;
			for (const profile of profiles) {
				if (!profile.enabledExtensions.includes(extensionId)) {
					profileStore.getState().setEnabledExtensions(profile.id, [
						...profile.enabledExtensions,
						extensionId,
					]);
				}
			}

			return ext;
		} catch (err) {
			console.error(`[CWS] Failed to install ${extensionId}:`, err);
			return null;
		}
	}

	async uninstall(extensionId: string): Promise<void> {
		const ext = cwsStore
			.getState()
			.extensions.find((e) => e.id === extensionId);

		if (ext && fs.existsSync(ext.path)) {
			fs.rmSync(ext.path, { recursive: true });
		}

		cwsStore.getState().uninstall(extensionId);

		const profiles = profileStore.getState().profiles;
		for (const profile of profiles) {
			if (profile.enabledExtensions.includes(extensionId)) {
				profileStore.getState().setEnabledExtensions(
					profile.id,
					profile.enabledExtensions.filter((id) => id !== extensionId),
				);
			}
		}
	}

	async checkForUpdates(): Promise<void> {
		const installed = cwsStore.getState().extensions;
		if (installed.length === 0) return;

		console.log(`[CWS] Checking updates for ${installed.length} extension(s)...`);
		const updated = await checkForUpdates(installed);

		for (const ext of updated) {
			cwsStore.getState().update(ext.id, {
				version: ext.version,
				path: ext.path,
				updatedAt: ext.updatedAt,
			});
			console.log(`[CWS] Updated ${ext.name} to ${ext.version}`);
		}

		if (updated.length === 0) {
			console.log("[CWS] All extensions up to date.");
		}
	}

	async loadExtensionsForProfile(profileId: string): Promise<void> {
		if (this.loadedProfiles.has(profileId)) return;
		this.loadedProfiles.add(profileId);

		const profile = profileStore
			.getState()
			.profiles.find((p) => p.id === profileId);
		if (!profile) return;

		const installed = cwsStore.getState().extensions;

		for (const extensionId of profile.enabledExtensions) {
			const ext = installed.find((e) => e.id === extensionId);
			if (ext) {
				await this.extensionManager.loadExtension(profileId, ext.path);
			}
		}
	}

	toggleExtension(profileId: string, extensionId: string): void {
		const profile = profileStore
			.getState()
			.profiles.find((p) => p.id === profileId);
		if (!profile) return;

		const enabled = profile.enabledExtensions.includes(extensionId);
		const next = enabled
			? profile.enabledExtensions.filter((id) => id !== extensionId)
			: [...profile.enabledExtensions, extensionId];

		profileStore.getState().setEnabledExtensions(profileId, next);
	}

	registerIpc(): void {
		ipcMain.handle(
			"cws:install",
			(_e: unknown, extensionId: string) => this.install(extensionId),
		);

		ipcMain.handle(
			"cws:uninstall",
			(_e: unknown, extensionId: string) => this.uninstall(extensionId),
		);

		ipcMain.handle("cws:installed", () =>
			cwsStore.getState().extensions,
		);

		ipcMain.handle(
			"cws:toggle",
			(_e: unknown, profileId: string, extensionId: string) => {
				this.toggleExtension(profileId, extensionId);
			},
		);
	}

	private subscribeToNewProfiles(): void {
		let previousIds = new Set(
			profileStore.getState().profiles.map((p) => p.id),
		);

		profileStore.subscribe((state) => {
			const currentIds = new Set(state.profiles.map((p) => p.id));
			const installedIds = cwsStore
				.getState()
				.extensions.map((e) => e.id);

			for (const profile of state.profiles) {
				if (!previousIds.has(profile.id)) {
					profileStore.getState().setEnabledExtensions(
						profile.id,
						installedIds,
					);
				}
			}

			previousIds = currentIds;
		});
	}

	private subscribeToRemovedProfiles(): void {
		profileStore.subscribe((state, prev) => {
			const currentIds = new Set(state.profiles.map((p) => p.id));
			for (const profile of prev.profiles) {
				if (!currentIds.has(profile.id)) {
					this.loadedProfiles.delete(profile.id);
				}
			}
		});
	}
}
```

- [ ] **Step 2: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extensions/cws-manager.ts
git commit -m "feat(desktop): add CwsManager orchestrator for CWS lifecycle"
```

---

### Task 7: Add `cws` namespace to preload bridge

**Files:**
- Modify: `apps/desktop/src/preload/index.ts:25-47`

- [ ] **Step 1: Add the `cws` namespace to the `pane` object**

In `apps/desktop/src/preload/index.ts`, add the `cws` property to the `pane` object (after the `extensions` block, before the closing `}`):

```ts
const pane = {
	tabs: {
		open: (profileId: string, url?: string) =>
			ipcRenderer.invoke("tabs:open", profileId, url),
		close: (tabId: string) => ipcRenderer.invoke("tabs:close", tabId),
		switch: (tabId: string) => ipcRenderer.invoke("tabs:switch", tabId),
		navigate: (url: string) => ipcRenderer.invoke("tabs:navigate", url),
		goBack: () => ipcRenderer.invoke("tabs:go-back"),
		goForward: () => ipcRenderer.invoke("tabs:go-forward"),
		reload: () => ipcRenderer.invoke("tabs:reload"),
		hideAll: () => ipcRenderer.invoke("tabs:hide-all"),
		showActive: () => ipcRenderer.invoke("tabs:show-active"),
	},
	settings: {
		detectBrowser: () => ipcRenderer.invoke("settings:detect-browser"),
	},
	extensions: {
		list: (profileId: string) =>
			ipcRenderer.invoke("extensions:list", profileId),
		load: (profileId: string, extPath: string) =>
			ipcRenderer.invoke("extensions:load", profileId, extPath),
	},
	cws: {
		install: (extensionId: string) =>
			ipcRenderer.invoke("cws:install", extensionId),
		uninstall: (extensionId: string) =>
			ipcRenderer.invoke("cws:uninstall", extensionId),
		installed: () => ipcRenderer.invoke("cws:installed"),
		toggle: (profileId: string, extensionId: string) =>
			ipcRenderer.invoke("cws:toggle", profileId, extensionId),
	},
};
```

- [ ] **Step 2: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/preload/index.ts
git commit -m "feat(desktop): add cws namespace to preload bridge"
```

---

### Task 8: Wire `CwsManager` into `index.ts` and remove temp hack

**Files:**
- Modify: `apps/desktop/src/main/index.ts`

This is the integration task. It modifies `index.ts` to:
1. Import and rehydrate `cwsStore`
2. Create `CwsManager`
3. Register CWS IPC
4. Hook extension loading into the tab creation callback
5. Fire update check at startup
6. Remove the temporary auto-load hack

- [ ] **Step 1: Add imports**

Add these imports to the top of `apps/desktop/src/main/index.ts` (after the existing imports):

```ts
import { cwsStore } from "../stores/cws-store";
import { CwsManager } from "./extensions/cws-manager";
```

- [ ] **Step 2: Add `cwsStore` to `StoreSync` and declare `cwsManager`**

Update the `StoreSync` constructor to include `cwsStore`:

```ts
const storeSync = new StoreSync({
	"profile-store": profileStore,
	"tab-store": tabStore,
	"navigation-store": navigationStore,
	"settings-store": settingsStore,
	"extension-store": extensionStore,
	"cws-store": cwsStore,
});
```

Add a `cwsManager` variable alongside `extensionManager`:

```ts
let extensionManager: ExtensionManager | null = null;
let cwsManager: CwsManager | null = null;
```

- [ ] **Step 3: Create `CwsManager` in `createWindow` and hook tab creation**

In the `createWindow()` function, after the `extensionManager` creation and tab callback wiring, create `cwsManager` and update the `onTabCreated` callback to load extensions:

```ts
extensionManager = new ExtensionManager(tabManager, mainWindow);
cwsManager = new CwsManager(extensionManager);

tabManager.onTabCreated = (wc, profileId) => {
	cwsManager!.loadExtensionsForProfile(profileId);
	extensionManager!.registerTab(wc, profileId);
};
tabManager.onTabActivated = (wc) => extensionManager!.activateTab(wc);
tabManager.onTabRemoved = (wc) => extensionManager!.unregisterTab(wc);
```

- [ ] **Step 4: Add `cwsStore` rehydration and CWS IPC registration**

In the `app.whenReady()` callback, add `cwsStore` rehydration (after the existing `profileStore` and `settingsStore` rehydration):

```ts
profileStore.persist.rehydrate();
settingsStore.persist.rehydrate();
cwsStore.persist.rehydrate();
```

After `extensionManager?.registerIpc()` and `tabManager.restore()`, add CWS IPC registration and the update check:

```ts
extensionManager?.registerIpc();
cwsManager?.registerIpc();
tabManager.restore();

cwsManager?.checkForUpdates().catch((err) => {
	console.error("[CWS] Update check failed:", err);
});
```

- [ ] **Step 5: Remove the temporary auto-load hack**

Delete lines 147–163 of the original `index.ts` — the entire `// TODO: remove — temporary auto-load for testing` block:

```ts
// DELETE THIS ENTIRE BLOCK:
// TODO: remove — temporary auto-load for testing
const extPath = `${app.getPath("appData")}/dolphin_anty/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd`;
const loadedProfiles = new Set<string>();
function tryLoadExt() {
	const profiles = profileStore.getState().profiles;
	for (const profile of profiles) {
		if (!loadedProfiles.has(profile.id)) {
			loadedProfiles.add(profile.id);
			console.log("[AutoLoad] loading extension for profile:", profile.id);
			extensionManager?.loadExtension(profile.id, extPath)
				.then((ext) => console.log("[AutoLoad] loaded:", ext?.name))
				.catch((e) => console.error("[AutoLoad] error:", e));
		}
	}
}
tryLoadExt();
profileStore.subscribe(() => tryLoadExt());
```

The profile deletion subscription (lines 165–172) stays — it handles `ExtensionManager.destroyProfile()`. The `CwsManager` constructor already subscribes to handle its own cleanup of the `loadedProfiles` set.

- [ ] **Step 6: Verify the final `index.ts` structure**

The `app.whenReady()` block should now look like:

```ts
app.whenReady().then(() => {
	profileStore.persist.rehydrate();
	settingsStore.persist.rehydrate();
	cwsStore.persist.rehydrate();

	if (!settingsStore.getState().settings.chromiumPath) {
		const detected = detectBrowserPath();
		if (detected) {
			settingsStore.getState().save({ chromiumPath: detected });
		}
	}

	storeSync.register();
	tabManager.registerIpc();

	ipcMain.handle("settings:detect-browser", () => {
		const detected = detectBrowserPath();
		if (detected) {
			settingsStore.getState().save({ chromiumPath: detected });
		}
		return detected;
	});

	const menu = Menu.buildFromTemplate([/* ... unchanged ... */]);
	Menu.setApplicationMenu(menu);

	createWindow();
	extensionManager?.registerIpc();
	cwsManager?.registerIpc();
	tabManager.restore();

	cwsManager?.checkForUpdates().catch((err) => {
		console.error("[CWS] Update check failed:", err);
	});

	profileStore.subscribe((state, prev) => {
		const removedIds = prev.profiles
			.filter((p) => !state.profiles.find((s) => s.id === p.id))
			.map((p) => p.id);
		for (const id of removedIds) {
			extensionManager?.destroyProfile(id);
		}
	});

	app.on("activate", () => {
		if (!mainWindow) {
			createWindow();
			tabManager.restore();
		}
	});
});
```

- [ ] **Step 7: Build and verify**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 8: Commit**

```bash
git add apps/desktop/src/main/index.ts
git commit -m "feat(desktop): wire CwsManager and remove temp extension auto-load hack"
```

---

### Task 9: Manual smoke test

- [ ] **Step 1: Launch the app**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite dev
```

- [ ] **Step 2: Verify clean startup**

Check the terminal output for:
- No crash on startup
- `[CWS] All extensions up to date.` or `[CWS] Checking updates for 0 extension(s)...` (since nothing is installed yet)
- No `[AutoLoad]` messages (the temp hack is gone)

- [ ] **Step 3: Test install via dev tools console**

Open the renderer devtools (`Cmd+Option+I`), then run:

```js
await window.pane.cws.install("eiaeiblijfjekdanodkjadfinkhbfgcd")
```

This should download NordPass from CWS. Check the terminal for download progress. Verify the return value has `id`, `name`, `version`, `path`.

- [ ] **Step 4: Verify the extension is on disk**

```bash
ls -la "$HOME/Library/Application Support/@pane/desktop/Extensions/eiaeiblijfjekdanodkjadfinkhbfgcd/"
```

Expected: a version directory (e.g., `7.9.1/`) containing the unpacked extension with `manifest.json`.

- [ ] **Step 5: Verify it persisted**

```bash
cat "$HOME/Library/Application Support/@pane/desktop/cws-extensions.json"
```

Expected: JSON with the installed extension data.

- [ ] **Step 6: Test extension loading with a profile**

Create a profile in the UI, then open a tab. Check the terminal for extension loading messages. The NordPass icon should appear in the browser action area.

- [ ] **Step 7: Test list and toggle**

In the renderer devtools:

```js
// List installed
await window.pane.cws.installed()

// Toggle off for a profile (use your profile ID)
await window.pane.cws.toggle("YOUR_PROFILE_ID", "eiaeiblijfjekdanodkjadfinkhbfgcd")
```

- [ ] **Step 8: Test uninstall**

```js
await window.pane.cws.uninstall("eiaeiblijfjekdanodkjadfinkhbfgcd")
```

Verify the extension directory is gone and `cws-extensions.json` is empty.

- [ ] **Step 9: Test update check**

Install the extension again, then restart the app. Check terminal for `[CWS] Checking updates for 1 extension(s)...` followed by either an update message or `All extensions up to date.`
