# Pane Architecture Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the desktop app from scattered singletons (`TabManager`, `ExtensionManager`, `CwsManager`) into a domain-driven class hierarchy (`Pane` > `Profile` > `ProfileTabs` / `ProfileExtensions`) and replace custom CWS code with `electron-chrome-web-store`.

**Architecture:** `Pane` is the top-level class owning profile collection and global extension operations. `Profile` encapsulates per-profile runtime state (session, ECE, tabs, extensions) with nested context objects `profile.tabs` and `profile.extensions`. Stores remain source of truth for serializable data; classes own runtime-only state.

**Tech Stack:** Electron 41.4.0, `@pane/electron-chrome-extensions` (ECE fork), `electron-chrome-web-store` (ECWS), Zustand 5, TypeScript.

**Spec:** `docs/superpowers/specs/2026-05-02-pane-architecture-redesign.md`

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `apps/desktop/src/main/profile-tabs.ts` | `ProfileTabs` — tab lifecycle per profile |
| Create | `apps/desktop/src/main/profile-extensions.ts` | `ProfileExtensions` — per-session extension loading |
| Create | `apps/desktop/src/main/profile.ts` | `Profile` — session, ECE, nested contexts |
| Create | `apps/desktop/src/main/pane-extensions.ts` | `PaneExtensions` — global CWS operations via ECWS |
| Create | `apps/desktop/src/main/pane.ts` | `Pane` — profile collection, IPC, startup |
| Modify | `apps/desktop/src/stores/profile-store.ts` | Remove `enabledExtensions`, `setEnabledExtensions` |
| Modify | `apps/desktop/src/main/index.ts` | Replace all singletons with `new Pane()` |
| Modify | `apps/desktop/src/preload/index.ts` | Remove `cws:toggle`, keep rest |
| Modify | `apps/desktop/package.json` | Add ECWS, remove unzip-crx-3 |
| Delete | `apps/desktop/src/main/extensions/cws-downloader.ts` | Replaced by ECWS |
| Delete | `apps/desktop/src/main/extensions/cws-updater.ts` | Replaced by ECWS |
| Delete | `apps/desktop/src/main/extensions/cws-manager.ts` | Absorbed into Pane + PaneExtensions |
| Delete | `apps/desktop/src/main/extensions/extension-manager.ts` | Absorbed into Profile |
| Delete | `apps/desktop/src/main/browser/tab-manager.ts` | Absorbed into ProfileTabs |
| Delete | `apps/desktop/src/stores/cws-store.ts` | ECWS uses directory as source of truth |

---

### Task 1: Swap dependencies

**Files:**
- Modify: `apps/desktop/package.json`

- [ ] **Step 1: Remove unzip-crx-3 and add electron-chrome-web-store**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && bun remove unzip-crx-3 && bun add electron-chrome-web-store
```

- [ ] **Step 2: Verify install**

```bash
cd /Users/andrevictor/www/pane && cat apps/desktop/package.json | grep -E "(electron-chrome-web-store|unzip-crx)"
```

Expected: `"electron-chrome-web-store"` present, `"unzip-crx-3"` absent.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/package.json bun.lock
git commit -m "deps(desktop): swap unzip-crx-3 for electron-chrome-web-store"
```

---

### Task 2: Remove `enabledExtensions` from `BrowserProfile`

**Files:**
- Modify: `apps/desktop/src/stores/profile-store.ts`

- [ ] **Step 1: Remove `enabledExtensions` from the `BrowserProfile` interface**

Remove this line from the interface:

```ts
	enabledExtensions: string[];
```

- [ ] **Step 2: Remove `enabledExtensions` from the `CreateInput` exclusion**

Change:

```ts
type CreateInput = Omit<
	BrowserProfile,
	"id" | "createdAt" | "updatedAt" | "tabs" | "isExpanded" | "enabledExtensions"
>;
```

Back to:

```ts
type CreateInput = Omit<
	BrowserProfile,
	"id" | "createdAt" | "updatedAt" | "tabs" | "isExpanded"
>;
```

- [ ] **Step 3: Remove `enabledExtensions: []` from the `create` action**

In the `create` action, remove the `enabledExtensions: [],` line from the new profile object.

- [ ] **Step 4: Remove `setEnabledExtensions` from `ProfileState` interface and implementation**

Remove from the interface:

```ts
	setEnabledExtensions: (profileId: string, extensionIds: string[]) => void;
```

Remove the implementation:

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

- [ ] **Step 5: Remove `enabledExtensions` fallback from `merge` handler**

Remove this line from the merge function:

```ts
						enabledExtensions: p.enabledExtensions ?? [],
```

- [ ] **Step 6: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -5
```

Expected: Build will FAIL because `cws-manager.ts` references `enabledExtensions` and `setEnabledExtensions`. That's fine — those files will be deleted in a later task. If it fails for only those reasons, proceed.

- [ ] **Step 7: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts
git commit -m "refactor(desktop): remove enabledExtensions from BrowserProfile"
```

---

### Task 3: Create `ProfileTabs`

**Files:**
- Create: `apps/desktop/src/main/profile-tabs.ts`

This absorbs all tab logic from `TabManager`. The key differences: it operates on a single profile (not all profiles), has no IPC registration (Pane handles IPC), and receives the ECE instance from Profile.

- [ ] **Step 1: Create the file**

Create `apps/desktop/src/main/profile-tabs.ts`:

```ts
import {
	type BaseWindow,
	session,
	type WebContents,
	WebContentsView,
} from "electron";

import { type BrowserProfile, profileStore } from "../stores/profile-store";
import { tabStore } from "../stores/tab-store";

const SIDEBAR_WIDTH = 220;
const TOOLBAR_HEIGHT = 51;
const PANEL_MARGIN_RIGHT = 8;
const PANEL_MARGIN_BOTTOM = 8;

interface TabHost {
	readonly id: string;
	readonly session: Electron.Session;
	readonly ece: import("@pane/electron-chrome-extensions").ElectronChromeExtensions;
	readonly extensions: { ensureLoaded(): Promise<void> };
	get data(): BrowserProfile;
}

export class ProfileTabs {
	private readonly views = new Map<string, WebContentsView>();

	constructor(
		private readonly profile: TabHost,
		private readonly mainWindow: BaseWindow,
	) {}

	open(url?: string, tabId?: string): WebContentsView {
		const id = tabId ?? crypto.randomUUID();
		const targetUrl = url || "https://www.google.com";
		const view = this.createView(id);

		this.views.set(id, view);
		this.mainWindow.contentView.addChildView(view);
		view.webContents.loadURL(targetUrl);

		this.profile.extensions.ensureLoaded();
		this.profile.ece.addTab(view.webContents, this.mainWindow);

		profileStore.getState().openTab(this.profile.id, id, targetUrl);
		this.activate(id);

		return view;
	}

	close(tabId: string): void {
		const view = this.views.get(tabId);
		if (!view) return;

		this.profile.ece.removeTab(view.webContents);
		this.mainWindow.contentView.removeChildView(view);
		view.webContents.close();
		this.views.delete(tabId);
		profileStore.getState().closeTab(tabId);

		if (tabStore.getState().activeTabId === tabId) {
			const allTabs = profileStore.getState().profiles.flatMap((p) => p.tabs);
			const nextId = allTabs[allTabs.length - 1]?.id ?? null;
			tabStore.getState().setActiveTab(nextId);
			if (nextId) {
				for (const [, v] of this.views) {
					// Only show if this profile owns the next tab
				}
			}
		}
	}

	activate(tabId: string): void {
		const { activeTabId } = tabStore.getState();

		if (activeTabId) {
			this.findViewGlobally(activeTabId)?.setVisible(false);
		}

		const activeView = this.views.get(tabId);
		activeView?.setVisible(true);
		tabStore.getState().setActiveTab(tabId);

		if (activeView) {
			this.profile.ece.selectTab(activeView.webContents);
		}
	}

	closeAll(): void {
		for (const tabId of [...this.views.keys()]) {
			this.close(tabId);
		}
	}

	resizeAll(): void {
		const bounds = this.getContentBounds();
		for (const view of this.views.values()) {
			view.setBounds(bounds);
		}
	}

	navigate(url: string): void {
		this.activeView()?.webContents.loadURL(normalizeUrl(url));
	}

	goBack(): void {
		this.activeView()?.webContents.goBack();
	}

	goForward(): void {
		this.activeView()?.webContents.goForward();
	}

	reload(): void {
		this.activeView()?.webContents.reload();
	}

	hideAll(): void {
		for (const view of this.views.values()) {
			view.setVisible(false);
		}
	}

	showActive(): void {
		this.activeView()?.setVisible(true);
	}

	has(tabId: string): boolean {
		return this.views.has(tabId);
	}

	getWebContents(tabId: string): WebContents | undefined {
		return this.views.get(tabId)?.webContents;
	}

	activateByWebContents(webContents: WebContents): void {
		for (const [tabId, view] of this.views) {
			if (view.webContents === webContents) {
				this.activate(tabId);
				return;
			}
		}
	}

	destroyByWebContents(webContents: WebContents): void {
		for (const [tabId, view] of this.views) {
			if (view.webContents === webContents) {
				this.close(tabId);
				return;
			}
		}
	}

	openForExtension(url: string): WebContentsView {
		const tabId = crypto.randomUUID();
		const view = this.createView(tabId);

		this.views.set(tabId, view);
		this.mainWindow.contentView.addChildView(view);
		view.webContents.loadURL(url);

		profileStore.getState().openTab(this.profile.id, tabId, url);
		this.activate(tabId);

		return view;
	}

	private activeView(): WebContentsView | undefined {
		const { activeTabId } = tabStore.getState();
		return activeTabId ? this.views.get(activeTabId) : undefined;
	}

	private findViewGlobally(tabId: string): WebContentsView | undefined {
		return this.views.get(tabId);
	}

	private createView(tabId: string): WebContentsView {
		const profile = this.profile.data;
		const partition = `persist:profile-${this.profile.id}`;

		if (profile.proxy) {
			const p = profile.proxy;
			session
				.fromPartition(partition)
				.setProxy({ proxyRules: `${p.proxyType}://${p.host}:${p.port}` })
				.catch(() => {});
		}

		const view = new WebContentsView({
			webPreferences: { partition, contextIsolation: true, sandbox: true },
		});

		if (profile.fingerprint.userAgent) {
			view.webContents.setUserAgent(profile.fingerprint.userAgent);
		}

		view.webContents.on("did-navigate", (_e, url) => {
			profileStore.getState().updateTab(tabId, { url });
		});

		view.webContents.on("did-navigate-in-page", (_e, url) => {
			profileStore.getState().updateTab(tabId, { url });
		});

		view.webContents.on("page-title-updated", (_e, title) => {
			profileStore.getState().updateTab(tabId, { title });
		});

		view.webContents.on("page-favicon-updated", (_e, favicons) => {
			profileStore.getState().updateTab(tabId, { favicon: favicons[0] ?? "" });
		});

		view.setBounds(this.getContentBounds());
		view.setBorderRadius(10);
		view.setVisible(false);

		return view;
	}

	private getContentBounds(): Electron.Rectangle {
		const [width, height] = this.mainWindow.getContentSize();
		return {
			x: SIDEBAR_WIDTH,
			y: TOOLBAR_HEIGHT,
			width: width - SIDEBAR_WIDTH - PANEL_MARGIN_RIGHT,
			height: height - TOOLBAR_HEIGHT - PANEL_MARGIN_BOTTOM,
		};
	}
}

function normalizeUrl(url: string): string {
	if (url.startsWith("http://") || url.startsWith("https://")) {
		return url;
	}
	if (url.startsWith("localhost") || (url.includes(".") && !url.includes(" "))) {
		return `https://${url}`;
	}
	return `https://www.google.com/search?q=${encodeURIComponent(url)}`;
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/profile-tabs.ts
git commit -m "feat(desktop): add ProfileTabs — per-profile tab lifecycle"
```

---

### Task 4: Create `ProfileExtensions`

**Files:**
- Create: `apps/desktop/src/main/profile-extensions.ts`

- [ ] **Step 1: Create the file**

Create `apps/desktop/src/main/profile-extensions.ts`:

```ts
import type { Extension, Session } from "electron";
import { loadAllExtensions } from "electron-chrome-web-store";

export class ProfileExtensions {
	private loaded = false;

	constructor(
		private readonly session: Session,
		private readonly extensionsPath: string,
	) {}

	async ensureLoaded(): Promise<void> {
		if (this.loaded) return;
		this.loaded = true;
		await loadAllExtensions(this.session, this.extensionsPath);
	}

	unload(extensionId: string): void {
		this.session.extensions.removeExtension(extensionId);
	}

	getLoaded(): Extension[] {
		return this.session.extensions.getAllExtensions();
	}
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/profile-extensions.ts
git commit -m "feat(desktop): add ProfileExtensions — per-session extension loading"
```

---

### Task 5: Create `Profile`

**Files:**
- Create: `apps/desktop/src/main/profile.ts`

- [ ] **Step 1: Create the file**

Create `apps/desktop/src/main/profile.ts`:

```ts
import { type BaseWindow, session } from "electron";
import { ElectronChromeExtensions } from "@pane/electron-chrome-extensions";

import { type BrowserProfile, profileStore } from "../stores/profile-store";
import { extensionStore } from "../stores/extension-store";
import { ProfileTabs } from "./profile-tabs";
import { ProfileExtensions } from "./profile-extensions";

export class Profile {
	readonly session: Electron.Session;
	readonly ece: ElectronChromeExtensions;
	readonly tabs: ProfileTabs;
	readonly extensions: ProfileExtensions;

	constructor(
		readonly id: string,
		private readonly mainWindow: BaseWindow,
		extensionsPath: string,
	) {
		this.session = session.fromPartition(`persist:profile-${id}`);
		ElectronChromeExtensions.handleCRXProtocol(this.session);

		this.extensions = new ProfileExtensions(this.session, extensionsPath);

		this.ece = new ElectronChromeExtensions({
			license: "GPL-3.0",
			session: this.session,
			createTab: async (details) => {
				const view = this.tabs.openForExtension(details.url || "about:blank");
				return [view.webContents, this.mainWindow];
			},
			selectTab: (wc) => this.tabs.activateByWebContents(wc),
			removeTab: (wc) => this.tabs.destroyByWebContents(wc),
		});

		this.tabs = new ProfileTabs(this, mainWindow);
	}

	get data(): BrowserProfile {
		return profileStore.getState().profiles.find((p) => p.id === this.id)!;
	}

	destroy(): void {
		this.tabs.closeAll();
		this.ece.destroy();
		extensionStore.getState().clearProfile(this.id);
	}
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/profile.ts
git commit -m "feat(desktop): add Profile — session, ECE, tabs, extensions"
```

---

### Task 6: Create `PaneExtensions`

**Files:**
- Create: `apps/desktop/src/main/pane-extensions.ts`

- [ ] **Step 1: Create the file**

Create `apps/desktop/src/main/pane-extensions.ts`:

```ts
import fs from "node:fs";
import path from "node:path";
import type { Extension } from "electron";
import {
	installExtension,
	uninstallExtension,
	updateExtensions,
} from "electron-chrome-web-store";

import type { Profile } from "./profile";

export interface InstalledExtension {
	id: string;
	name: string;
	version: string;
	path: string;
}

interface PaneExtensionsHost {
	allProfiles(): Profile[];
}

export class PaneExtensions {
	constructor(
		private readonly host: PaneExtensionsHost,
		private readonly extensionsPath: string,
	) {}

	async install(extensionId: string): Promise<Extension | null> {
		try {
			const ext = await installExtension(extensionId, {
				extensionsPath: this.extensionsPath,
			});
			for (const profile of this.host.allProfiles()) {
				await profile.extensions.ensureLoaded();
			}
			return ext;
		} catch (err) {
			console.error(`[CWS] Failed to install ${extensionId}:`, err);
			return null;
		}
	}

	async uninstall(extensionId: string): Promise<void> {
		for (const profile of this.host.allProfiles()) {
			profile.extensions.unload(extensionId);
		}
		await uninstallExtension(extensionId, {
			extensionsPath: this.extensionsPath,
		});
	}

	installed(): InstalledExtension[] {
		const result: InstalledExtension[] = [];
		if (!fs.existsSync(this.extensionsPath)) return result;

		for (const extId of fs.readdirSync(this.extensionsPath)) {
			const extDir = path.join(this.extensionsPath, extId);
			if (!fs.statSync(extDir).isDirectory()) continue;

			for (const version of fs.readdirSync(extDir)) {
				const manifestPath = path.join(extDir, version, "manifest.json");
				if (!fs.existsSync(manifestPath)) continue;

				try {
					const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf-8"));
					let name: string = manifest.name ?? extId;
					if (name.startsWith("__MSG_") && name.endsWith("__")) {
						const msgKey = name.slice(6, -2);
						try {
							const messagesPath = path.join(extDir, version, "_locales", "en", "messages.json");
							const messages = JSON.parse(fs.readFileSync(messagesPath, "utf-8"));
							name = messages[msgKey]?.message ?? name;
						} catch {}
					}
					result.push({
						id: extId,
						name,
						version: manifest.version ?? version,
						path: path.join(extDir, version),
					});
				} catch {}
			}
		}
		return result;
	}

	async checkForUpdates(): Promise<void> {
		const profiles = this.host.allProfiles();
		if (profiles.length === 0) return;
		try {
			await updateExtensions(profiles[0].session);
		} catch (err) {
			console.error("[CWS] Update check failed:", err);
		}
	}
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/pane-extensions.ts
git commit -m "feat(desktop): add PaneExtensions — global CWS via ECWS"
```

---

### Task 7: Create `Pane`

**Files:**
- Create: `apps/desktop/src/main/pane.ts`

- [ ] **Step 1: Create the file**

Create `apps/desktop/src/main/pane.ts`:

```ts
import path from "node:path";
import { app, type BaseWindow, ipcMain } from "electron";

import { profileStore } from "../stores/profile-store";
import { tabStore } from "../stores/tab-store";
import { extensionStore } from "../stores/extension-store";
import { detectBrowserPath } from "./browser/detect-browser";
import { Profile } from "./profile";
import { PaneExtensions } from "./pane-extensions";

export class Pane {
	readonly extensions: PaneExtensions;
	private readonly profiles = new Map<string, Profile>();
	private readonly extensionsPath: string;

	constructor(private readonly mainWindow: BaseWindow) {
		this.extensionsPath = path.join(app.getPath("userData"), "Extensions");
		this.extensions = new PaneExtensions(this, this.extensionsPath);
	}

	createProfile(input: Parameters<typeof profileStore.getState.create>[0]): Profile {
		profileStore.getState().create(input);
		const profiles = profileStore.getState().profiles;
		const data = profiles[profiles.length - 1];
		const profile = new Profile(data.id, this.mainWindow, this.extensionsPath);
		this.profiles.set(data.id, profile);
		return profile;
	}

	getProfile(id: string): Profile | undefined {
		return this.profiles.get(id);
	}

	removeProfile(id: string): void {
		this.profiles.get(id)?.destroy();
		this.profiles.delete(id);
		profileStore.getState().remove(id);
	}

	allProfiles(): Profile[] {
		return [...this.profiles.values()];
	}

	restore(): void {
		for (const data of profileStore.getState().profiles) {
			const profile = new Profile(data.id, this.mainWindow, this.extensionsPath);
			this.profiles.set(data.id, profile);
			for (const tab of data.tabs) {
				profile.tabs.open(tab.url, tab.id);
			}
		}

		this.extensions.checkForUpdates().catch((err) => {
			console.error("[CWS] Update check failed:", err);
		});
	}

	resizeAllTabs(): void {
		for (const profile of this.profiles.values()) {
			profile.tabs.resizeAll();
		}
	}

	registerIpc(): void {
		ipcMain.handle("tabs:open", (_e, profileId: string, url?: string) => {
			this.getProfile(profileId)?.tabs.open(url);
		});

		ipcMain.handle("tabs:close", (_e, tabId: string) => {
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(tabId)) {
					profile.tabs.close(tabId);
					return;
				}
			}
		});

		ipcMain.handle("tabs:switch", (_e, tabId: string) => {
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(tabId)) {
					profile.tabs.activate(tabId);
					return;
				}
			}
		});

		ipcMain.handle("tabs:navigate", (_e, url: string) => {
			const activeTabId = tabStore.getState().activeTabId;
			if (!activeTabId) return;
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(activeTabId)) {
					profile.tabs.navigate(url);
					return;
				}
			}
		});

		ipcMain.handle("tabs:go-back", () => {
			const activeTabId = tabStore.getState().activeTabId;
			if (!activeTabId) return;
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(activeTabId)) {
					profile.tabs.goBack();
					return;
				}
			}
		});

		ipcMain.handle("tabs:go-forward", () => {
			const activeTabId = tabStore.getState().activeTabId;
			if (!activeTabId) return;
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(activeTabId)) {
					profile.tabs.goForward();
					return;
				}
			}
		});

		ipcMain.handle("tabs:reload", () => {
			const activeTabId = tabStore.getState().activeTabId;
			if (!activeTabId) return;
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(activeTabId)) {
					profile.tabs.reload();
					return;
				}
			}
		});

		ipcMain.handle("tabs:hide-all", () => {
			for (const profile of this.profiles.values()) {
				profile.tabs.hideAll();
			}
		});

		ipcMain.handle("tabs:show-active", () => {
			const activeTabId = tabStore.getState().activeTabId;
			if (!activeTabId) return;
			for (const profile of this.profiles.values()) {
				if (profile.tabs.has(activeTabId)) {
					profile.tabs.showActive();
					return;
				}
			}
		});

		ipcMain.handle("cws:install", (_e, extensionId: string) =>
			this.extensions.install(extensionId),
		);

		ipcMain.handle("cws:uninstall", (_e, extensionId: string) =>
			this.extensions.uninstall(extensionId),
		);

		ipcMain.handle("cws:installed", () =>
			this.extensions.installed(),
		);

		ipcMain.handle("extensions:list", (_e, profileId: string) => {
			const loaded = this.getProfile(profileId)?.extensions.getLoaded() ?? [];
			return loaded.map((ext) => ({
				id: ext.id,
				name: ext.name,
				version: ext.manifest.version,
			}));
		});

		ipcMain.handle("settings:detect-browser", () => {
			const detected = detectBrowserPath();
			if (detected) {
				const { settingsStore } = require("../stores/settings-store");
				settingsStore.getState().save({ chromiumPath: detected });
			}
			return detected;
		});
	}
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/pane.ts
git commit -m "feat(desktop): add Pane — top-level orchestrator with IPC"
```

---

### Task 8: Rewrite `index.ts`

**Files:**
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Replace the entire file**

Replace the contents of `apps/desktop/src/main/index.ts` with:

```ts
import path from "node:path";
import {
	app,
	BaseWindow,
	Menu,
	WebContentsView,
} from "electron";

import { extensionStore } from "../stores/extension-store";
import { navigationStore } from "../stores/navigation-store";
import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { detectBrowserPath } from "./browser/detect-browser";
import { Pane } from "./pane";
import { StoreSync } from "./store-sync";

let mainWindow: BaseWindow | null = null;
let uiView: WebContentsView | null = null;
let pane: Pane | null = null;

const storeSync = new StoreSync({
	"profile-store": profileStore,
	"tab-store": tabStore,
	"navigation-store": navigationStore,
	"settings-store": settingsStore,
	"extension-store": extensionStore,
});

function createWindow() {
	mainWindow = new BaseWindow({
		width: 1280,
		height: 800,
		minWidth: 900,
		minHeight: 600,
		title: "Pane",
		titleBarStyle: "hiddenInset",
		backgroundColor: "#09090b",
	});

	uiView = new WebContentsView({
		webPreferences: {
			preload: path.join(__dirname, "../preload/index.mjs"),
			contextIsolation: true,
			sandbox: false,
		},
	});

	storeSync.connect(uiView.webContents);

	pane = new Pane(mainWindow);

	mainWindow.contentView.addChildView(uiView);

	const [width, height] = mainWindow.getContentSize();
	uiView.setBounds({ x: 0, y: 0, width, height });

	if (process.env.ELECTRON_RENDERER_URL) {
		uiView.webContents.loadURL(process.env.ELECTRON_RENDERER_URL);
	} else {
		uiView.webContents.loadFile(path.join(__dirname, "../renderer/index.html"));
	}

	mainWindow.on("resized", () => {
		if (!mainWindow) return;
		const [w, h] = mainWindow.getContentSize();
		uiView?.setBounds({ x: 0, y: 0, width: w, height: h });
		pane?.resizeAllTabs();
	});

	mainWindow.on("closed", () => {
		mainWindow = null;
		uiView = null;
		pane = null;
	});
}

app.whenReady().then(() => {
	profileStore.persist.rehydrate();
	settingsStore.persist.rehydrate();

	if (!settingsStore.getState().settings.chromiumPath) {
		const detected = detectBrowserPath();
		if (detected) {
			settingsStore.getState().save({ chromiumPath: detected });
		}
	}

	storeSync.register();

	const menu = Menu.buildFromTemplate([
		{ role: "appMenu" },
		{ role: "fileMenu" },
		{ role: "editMenu" },
		{
			label: "View",
			submenu: [
				{
					label: "Toggle developer tools",
					accelerator: "CommandOrControl+Option+I",
					click: () => uiView?.webContents.openDevTools({ mode: "detach" }),
				},
				{ role: "reload", click: () => uiView?.webContents.reload() },
				{ type: "separator" },
				{ role: "resetZoom" },
				{ role: "zoomIn" },
				{ role: "zoomOut" },
				{ type: "separator" },
				{ role: "togglefullscreen" },
			],
		},
		{ role: "windowMenu" },
	]);
	Menu.setApplicationMenu(menu);

	createWindow();
	pane?.registerIpc();
	pane?.restore();

	app.on("activate", () => {
		if (!mainWindow) {
			createWindow();
			pane?.restore();
		}
	});
});

app.on("window-all-closed", () => {
	if (process.platform !== "darwin") {
		app.quit();
	}
});
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/index.ts
git commit -m "refactor(desktop): rewrite index.ts to use Pane class"
```

---

### Task 9: Update preload bridge

**Files:**
- Modify: `apps/desktop/src/preload/index.ts`

- [ ] **Step 1: Remove `cws:toggle` from preload and `extensions:load`**

The `cws:toggle` channel no longer exists (per-profile enable/disable is dropped). Also remove `extensions:load` (no longer needed — extensions load automatically). Update the `pane` object:

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
	},
	cws: {
		install: (extensionId: string) =>
			ipcRenderer.invoke("cws:install", extensionId),
		uninstall: (extensionId: string) =>
			ipcRenderer.invoke("cws:uninstall", extensionId),
		installed: () => ipcRenderer.invoke("cws:installed"),
	},
};
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/preload/index.ts
git commit -m "refactor(desktop): simplify preload bridge — remove toggle and load"
```

---

### Task 10: Delete old files

**Files:**
- Delete: `apps/desktop/src/main/extensions/cws-downloader.ts`
- Delete: `apps/desktop/src/main/extensions/cws-updater.ts`
- Delete: `apps/desktop/src/main/extensions/cws-manager.ts`
- Delete: `apps/desktop/src/main/extensions/extension-manager.ts`
- Delete: `apps/desktop/src/main/browser/tab-manager.ts`
- Delete: `apps/desktop/src/stores/cws-store.ts`

- [ ] **Step 1: Delete all old files**

```bash
cd /Users/andrevictor/www/pane
rm apps/desktop/src/main/extensions/cws-downloader.ts
rm apps/desktop/src/main/extensions/cws-updater.ts
rm apps/desktop/src/main/extensions/cws-manager.ts
rm apps/desktop/src/main/extensions/extension-manager.ts
rm apps/desktop/src/main/browser/tab-manager.ts
rm apps/desktop/src/stores/cws-store.ts
```

- [ ] **Step 2: Verify build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -10
```

Expected: Clean build. All imports now point to the new files. If there are import errors, fix them.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor(desktop): delete old singletons — TabManager, ExtensionManager, CwsManager, cws-store"
```

---

### Task 11: Build and smoke test

- [ ] **Step 1: Full build**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build 2>&1 | tail -15
```

Expected: Clean build, no errors.

- [ ] **Step 2: Launch the app**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite dev
```

- [ ] **Step 3: Verify clean startup**

Check terminal for no crashes. No `[AutoLoad]` messages.

- [ ] **Step 4: Test CWS install**

In renderer devtools (`Cmd+Option+I`):

```js
await window.pane.cws.install("eiaeiblijfjekdanodkjadfinkhbfgcd")
```

- [ ] **Step 5: Test tab creation**

Create a profile in the UI, open a tab. Extension should load automatically.

- [ ] **Step 6: Test CWS list and uninstall**

```js
await window.pane.cws.installed()
await window.pane.cws.uninstall("eiaeiblijfjekdanodkjadfinkhbfgcd")
await window.pane.cws.installed() // should be empty
```

- [ ] **Step 7: Test restart with persisted profiles**

Quit the app, relaunch. Profiles and tabs should restore. If extensions were installed, update check should run.
