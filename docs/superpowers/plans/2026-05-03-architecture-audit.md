# Pane Architecture Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Address 26 architecture audit findings across security, stability, fingerprinting, performance, and DX in the Pane Electron app.

**Architecture:** Five sequential sub-projects (SP1→SP5) ordered by priority. Each task is independent within its sub-project. No test framework exists — verification is manual (typecheck + launch). All changes are in `apps/desktop/src/`.

**Tech Stack:** Electron 41, TypeScript, Zustand vanilla stores, React 19, electron-vite, electron-chrome-extensions

---

## File Structure

### Modified files:
- `src/main/window.ts` — sandbox: true (Task 1)
- `src/main/ipc.ts` — sender validation, cleanup method, tab index lookups (Tasks 2, 6, 12)
- `src/main/index.ts` — pass trusted sender ID, cleanup on close, destroy views, flush on quit (Tasks 2, 6, 16, 17)
- `src/stores/middlewares/store-sync.ts` — sender validation, debounce broadcasts (Tasks 3, 9)
- `src/stores/middlewares/sync.ts` — single demux listener (Task 21)
- `src/stores/middlewares/fs-storage.ts` — flush pending writes (Task 17)
- `src/main/profile/profile-tabs.ts` — proxy in constructor, layout constants import, destroyAll (Tasks 5, 11, 16, 19)
- `src/main/profile/profile.ts` — proxy setup in constructor (Task 11)
- `src/main/pane.ts` — createProfile returns ID, tab index, lazy restore, dev extension guard (Tasks 7, 12, 15, 27)
- `src/stores/profile-store.ts` — create returns ID, Fingerprint interface, targeted updateTab/closeTab (Tasks 7, 14, 24)
- `src/main/extension-installer.ts` — async getInstalled, neutral session for updates, version sort (Tasks 8, 13, 20)
- `src/main/profile/extension-loader.ts` — retry cap, version sort (Tasks 20, 26)
- `src/renderer/components/sidebar/sidebar-connected.tsx` — granular selectors (Task 22)
- `src/renderer/pages/browser/_components/address-bar-connected.tsx` — merged useStore (Task 23)
- `src/renderer/components/default-fingerprints.ts` — new fingerprint fields (Task 14)
- `electron-vite.config.ts` — remove deprecated plugin (Task 25)

### New files:
- `src/constants/layout.ts` — shared layout constants (Task 19)
- `src/main/profile/fingerprint-preload.ts` — fingerprint override script (Task 10)
- `src/main/profile/fingerprint-validator.ts` — cross-API consistency checks (Task 10)

---

## SP1: Security Hardening

### Task 1: Enable sandbox on UI view

**Files:**
- Modify: `apps/desktop/src/main/window.ts:38`

- [ ] **Step 1: Change sandbox to true**

In `apps/desktop/src/main/window.ts`, change the `WebContentsView` webPreferences:

```ts
const uiView = new WebContentsView({
	webPreferences: {
		preload: path.join(__dirname, "../preload/index.mjs"),
		contextIsolation: true,
		sandbox: true,
	},
});
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Verify app launches correctly**

Run: `bun turbo run dev --filter=@pane/desktop`
Expected: App renders with sidebar and content panel. If `injectBrowserAction` fails, check the dev console — it may need refactoring to work within sandbox constraints. If it crashes, revert to `sandbox: false` and add a comment explaining why.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/window.ts
git commit -m "security: enable sandbox on UI renderer view"
```

---

### Task 2: IPC sender validation

**Files:**
- Modify: `apps/desktop/src/main/ipc.ts`
- Modify: `apps/desktop/src/main/index.ts:35`

- [ ] **Step 1: Add trustedSenderId to IpcRouter constructor and guard method**

Replace the entire `apps/desktop/src/main/ipc.ts`:

```ts
import { ipcMain, type IpcMainInvokeEvent } from "electron";

import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { detectBrowserPath } from "./detect-browser";
import type { Pane } from "./pane";
import type { Profile } from "./profile/profile";

export class IpcRouter {
	private readonly channels: string[] = [];

	constructor(
		private readonly pane: Pane,
		private readonly trustedSenderId: number,
	) {}

	register(): void {
		this.handle("profiles:activate", (_e, profileId: string) => {
			this.pane.getOrCreateProfile(profileId).extensions.ensureLoaded();
		});

		this.handle("tabs:open", (_e, profileId: string, url?: string) => {
			this.pane.hideAllTabs();
			this.pane.getOrCreateProfile(profileId).tabs.open(url);
		});

		this.handle("tabs:close", (_e, tabId: string) => {
			for (const profile of this.pane.profiles.values()) {
				if (profile.tabs.has(tabId)) {
					profile.tabs.close(tabId);

					return;
				}
			}
		});

		this.handle("tabs:switch", (_e, tabId: string) => {
			this.pane.hideAllTabs();

			for (const profile of this.pane.profiles.values()) {
				if (profile.tabs.has(tabId)) {
					profile.tabs.activate(tabId);

					return;
				}
			}
		});

		this.handle("tabs:navigate", (_e, url: string) => {
			this.findProfileForActiveTab()?.tabs.navigate(url);
		});

		this.handle("tabs:go-back", () => {
			this.findProfileForActiveTab()?.tabs.goBack();
		});

		this.handle("tabs:go-forward", () => {
			this.findProfileForActiveTab()?.tabs.goForward();
		});

		this.handle("tabs:reload", () => {
			this.findProfileForActiveTab()?.tabs.reload();
		});

		this.handle("tabs:hide-all", () => {
			this.pane.hideAllTabs();
		});

		this.handle("tabs:show-active", () => {
			this.findProfileForActiveTab()?.tabs.showActive();
		});

		this.handle("cws:install", (_e, extensionId: string) =>
			this.pane.extensions.install(extensionId),
		);

		this.handle("cws:uninstall", (_e, extensionId: string) =>
			this.pane.extensions.uninstall(extensionId),
		);

		this.handle("cws:installed", () => this.pane.extensions.getInstalled());

		this.handle("extensions:list", (_e, profileId: string) => {
			const loaded =
				this.pane.getProfile(profileId)?.extensions.getLoaded() ?? [];

			return loaded.map((ext) => ({
				id: ext.id,
				name: ext.name,
				version: ext.manifest.version,
			}));
		});

		this.handle("settings:detect-browser", () => {
			const detected = detectBrowserPath();

			if (detected) {
				settingsStore.getState().save({ chromiumPath: detected });
			}

			return detected;
		});
	}

	cleanup(): void {
		for (const channel of this.channels) {
			ipcMain.removeHandler(channel);
		}

		this.channels.length = 0;
	}

	private handle(
		channel: string,
		handler: (event: IpcMainInvokeEvent, ...args: any[]) => any,
	): void {
		this.channels.push(channel);

		ipcMain.handle(channel, (event, ...args) => {
			if (event.sender.id !== this.trustedSenderId) {
				return;
			}

			return handler(event, ...args);
		});
	}

	private findProfileForActiveTab(): Profile | undefined {
		const { activeProfileId } = tabStore.getState();

		return activeProfileId ? this.pane.getProfile(activeProfileId) : undefined;
	}
}
```

- [ ] **Step 2: Update index.ts to pass trustedSenderId**

In `apps/desktop/src/main/index.ts`, change the `IpcRouter` construction line from:

```ts
new IpcRouter(pane).register();
```

to:

```ts
new IpcRouter(pane, win.uiView.webContents.id).register();
```

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/ipc.ts apps/desktop/src/main/index.ts
git commit -m "security: validate IPC sender against trusted UI webContents"
```

---

### Task 3: Validate sync:push input

**Files:**
- Modify: `apps/desktop/src/stores/middlewares/store-sync.ts`
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Add sender validation and input validation to StoreSync**

Replace the entire `apps/desktop/src/stores/middlewares/store-sync.ts`:

```ts
import { ipcMain, type WebContents } from "electron";
import type { StoreApi } from "zustand/vanilla";
import { serializeState } from "./serialize";

type AnyStoreMap = { [name: string]: StoreApi<object> };

export class StoreSync<TMap extends AnyStoreMap> {
	private readonly stores: Map<string, TMap[keyof TMap]>;
	private target: WebContents | null = null;
	private trustedSenderId: number | null = null;

	constructor(stores: TMap) {
		this.stores = new Map(
			Object.entries(stores) as [string, TMap[keyof TMap]][],
		);
	}

	register() {
		ipcMain.on(
			"sync:push",
			(event, data: { store: string; state: string }) => {
				if (
					this.trustedSenderId !== null &&
					event.sender.id !== this.trustedSenderId
				) {
					return;
				}

				if (typeof data?.store !== "string" || typeof data?.state !== "string") {
					return;
				}

				const store = this.stores.get(data.store);

				if (!store) {
					return;
				}

				try {
					const partial = JSON.parse(data.state);
					store.setState((prev) => ({ ...prev, ...partial }));
				} catch {}
			},
		);

		ipcMain.handle("sync:get", (_event, storeName: string) => {
			const store = this.stores.get(storeName);

			return store ? serializeState(store.getState()) : null;
		});

		for (const [name, store] of this.stores) {
			store.subscribe(() =>
				this.broadcast(name, serializeState(store.getState())),
			);
		}
	}

	connect(webContents: WebContents) {
		this.target = webContents;
		this.trustedSenderId = webContents.id;

		webContents.once("destroyed", () => {
			this.target = null;
			this.trustedSenderId = null;
		});
	}

	private broadcast(storeName: string, state: string) {
		if (this.target && !this.target.isDestroyed()) {
			this.target.send("sync:push", { store: storeName, state });
		}
	}
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/middlewares/store-sync.ts
git commit -m "security: validate sender and input on sync:push IPC handler"
```

---

### Task 4: Configure Electron Fuses

**Files:**
- Modify: `apps/desktop/package.json`

- [ ] **Step 1: Add @electron/fuses dependency**

Run: `bun add -D @electron/fuses --filter @pane/desktop`

- [ ] **Step 2: Add afterPack script to electron-builder config**

In `apps/desktop/package.json`, add an `afterPack` script to the `build` section:

```json
"build": {
  "appId": "com.pane.desktop",
  "productName": "Pane",
  "afterPack": "./scripts/fuses.cjs",
  ...
}
```

- [ ] **Step 3: Create the fuses script**

Create `apps/desktop/scripts/fuses.cjs`:

```js
const { flipFuses, FuseVersion, FuseV1Options } = require("@electron/fuses");
const path = require("node:path");

module.exports = async function afterPack(context) {
	const ext = {
		darwin: ".app",
		linux: "",
		win32: ".exe",
	};

	const electronBinaryPath = path.join(
		context.appOutDir,
		`${context.packager.appInfo.productFilename}${ext[context.electronPlatformName] ?? ""}`,
	);

	await flipFuses(electronBinaryPath, {
		version: FuseVersion.V1,
		[FuseV1Options.RunAsNode]: false,
		[FuseV1Options.EnableNodeOptionsEnvironmentVariable]: false,
		[FuseV1Options.EnableNodeCliInspectArguments]: false,
		[FuseV1Options.EnableCookieEncryption]: true,
		[FuseV1Options.OnlyLoadAppFromAsar]: true,
	});
};
```

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/package.json apps/desktop/scripts/fuses.cjs
git commit -m "security: configure Electron Fuses for production hardening"
```

---

### Task 5: Surface proxy errors

**Files:**
- Modify: `apps/desktop/src/main/profile/profile-tabs.ts:195-201`

- [ ] **Step 1: Replace silent catch with error logging**

In `apps/desktop/src/main/profile/profile-tabs.ts`, replace the proxy setup block in `createView()`:

```ts
if (profile.proxy) {
	const p = profile.proxy;

	session
		.fromPartition(partition)
		.setProxy({ proxyRules: `${p.proxyType}://${p.host}:${p.port}` })
		.catch(() => {});
}
```

with:

```ts
if (profile.proxy) {
	const p = profile.proxy;

	session
		.fromPartition(partition)
		.setProxy({ proxyRules: `${p.proxyType}://${p.host}:${p.port}` })
		.catch((err) => {
			console.error(
				`[Profile ${this.profile.id}] Proxy failed to apply:`,
				err,
			);
		});
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/profile/profile-tabs.ts
git commit -m "security: log proxy configuration errors instead of swallowing them"
```

---

## SP2: Architecture Fixes

### Task 6: Fix duplicate IPC handler registration on macOS re-activate

**Files:**
- Modify: `apps/desktop/src/main/index.ts`

The `cleanup()` method was already added to `IpcRouter` in Task 2. Now wire it up in `index.ts`.

- [ ] **Step 1: Store ipcRouter reference and call cleanup on close**

In `apps/desktop/src/main/index.ts`, change the `setup()` function. Currently `IpcRouter` is created inline. Store a reference and clean up on close:

Replace this section in `setup()`:

```ts
pane = new Pane(win.mainWindow);
new IpcRouter(pane, win.uiView.webContents.id).register();
```

with:

```ts
pane = new Pane(win.mainWindow);
const ipcRouter = new IpcRouter(pane, win.uiView.webContents.id);
ipcRouter.register();
```

And replace the `closed` handler:

```ts
win.mainWindow.on("closed", () => {
	win = null;
	pane = null;
});
```

with:

```ts
win.mainWindow.on("closed", () => {
	ipcRouter.cleanup();
	win = null;
	pane = null;
});
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/index.ts
git commit -m "fix: clean up IPC handlers on window close to prevent macOS re-activate crash"
```

---

### Task 7: Fix createProfile array tail assumption

**Files:**
- Modify: `apps/desktop/src/stores/profile-store.ts:72-87`
- Modify: `apps/desktop/src/main/pane.ts:18-28`

- [ ] **Step 1: Make create() return the new profile ID**

In `apps/desktop/src/stores/profile-store.ts`, change the `create` method. Replace:

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
				isExpanded: false,
				createdAt: now,
				updatedAt: now,
			},
		],
	}));
},
```

with:

```ts
create: (input) => {
	const id = crypto.randomUUID();
	const now = new Date().toISOString();

	set((s) => ({
		profiles: [
			...s.profiles,
			{
				...input,
				id,
				tabs: [],
				isExpanded: false,
				createdAt: now,
				updatedAt: now,
			},
		],
	}));

	return id;
},
```

Also update the `ProfileState` interface — change `create: (input: CreateInput) => void` to `create: (input: CreateInput) => string`.

- [ ] **Step 2: Update Pane.createProfile to use returned ID**

In `apps/desktop/src/main/pane.ts`, replace:

```ts
createProfile(
	input: Parameters<ReturnType<typeof profileStore.getState>["create"]>[0],
): Profile {
	profileStore.getState().create(input);
	const profiles = profileStore.getState().profiles;
	const data = profiles[profiles.length - 1];
	const profile = new Profile(data.id, this.mainWindow, this.extensionsPath);
	this.profiles.set(data.id, profile);

	return profile;
}
```

with:

```ts
createProfile(
	input: Parameters<ReturnType<typeof profileStore.getState>["create"]>[0],
): Profile {
	const id = profileStore.getState().create(input);
	const profile = new Profile(id, this.mainWindow, this.extensionsPath);
	this.profiles.set(id, profile);

	return profile;
}
```

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts apps/desktop/src/main/pane.ts
git commit -m "fix: createProfile uses returned ID instead of array tail assumption"
```

---

### Task 8: Fix extension update check using arbitrary profile session

**Files:**
- Modify: `apps/desktop/src/main/extension-installer.ts:154-166`

- [ ] **Step 1: Use a neutral session for update checks**

In `apps/desktop/src/main/extension-installer.ts`, add `session` import:

```ts
import { session } from "electron";
```

(Add it alongside the existing `import type { Extension } from "electron";` — change to `import { session, type Extension } from "electron";`)

Then replace the `checkForUpdates` method:

```ts
async checkForUpdates(): Promise<void> {
	const profiles = [...this.host.profiles.values()];

	if (profiles.length === 0) {
		return;
	}

	try {
		await updateExtensions(profiles[0].session);
	} catch (err) {
		console.error("[CWS] Update check failed:", err);
	}
}
```

with:

```ts
async checkForUpdates(): Promise<void> {
	try {
		const updateSession = session.fromPartition("persist:pane-internal");
		await updateExtensions(updateSession);
	} catch (err) {
		console.error("[CWS] Update check failed:", err);
	}
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extension-installer.ts
git commit -m "fix: use neutral session for extension update checks instead of profile[0]"
```

---

### Task 9: Debounce state sync broadcasts

**Files:**
- Modify: `apps/desktop/src/stores/middlewares/store-sync.ts`

- [ ] **Step 1: Add debounced broadcast**

In `apps/desktop/src/stores/middlewares/store-sync.ts`, add a `timers` map and `scheduleBroadcast` method. Replace the `register()` method's subscribe loop and add the new private method.

Replace this block in `register()`:

```ts
for (const [name, store] of this.stores) {
	store.subscribe(() =>
		this.broadcast(name, serializeState(store.getState())),
	);
}
```

with:

```ts
for (const [name, store] of this.stores) {
	store.subscribe(() =>
		this.scheduleBroadcast(name, serializeState(store.getState())),
	);
}
```

Add the `timers` field to the class:

```ts
private readonly broadcastTimers = new Map<string, ReturnType<typeof setTimeout>>();
```

Add the `scheduleBroadcast` method:

```ts
private scheduleBroadcast(storeName: string, state: string) {
	const existing = this.broadcastTimers.get(storeName);

	if (existing) {
		clearTimeout(existing);
	}

	this.broadcastTimers.set(
		storeName,
		setTimeout(() => {
			this.broadcastTimers.delete(storeName);
			this.broadcast(storeName, state);
		}, 16),
	);
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/middlewares/store-sync.ts
git commit -m "perf: debounce store sync broadcasts with 16ms window"
```

---

## SP3: Fingerprint Engine

### Task 10: Fingerprint preload script and validator

**Files:**
- Create: `apps/desktop/src/main/profile/fingerprint-preload.ts`
- Create: `apps/desktop/src/main/profile/fingerprint-validator.ts`
- Modify: `apps/desktop/src/main/profile/profile.ts`

- [ ] **Step 1: Create fingerprint-validator.ts**

Create `apps/desktop/src/main/profile/fingerprint-validator.ts`:

```ts
import type { Fingerprint } from "../../stores/profile-store";

const VALID_DEVICE_MEMORY = [0.25, 0.5, 1, 2, 4, 8];

export function validateFingerprint(config: Fingerprint): string[] {
	const warnings: string[] = [];

	if (config.platform === "windows" && config.webgl) {
		if (/Apple|M[1-4]/i.test(config.webgl.renderer)) {
			warnings.push(
				"Windows platform with Apple/M-series WebGL renderer is inconsistent",
			);
		}
	}

	if (config.platform === "macos" && config.webgl) {
		if (!/Apple|AMD Radeon/i.test(config.webgl.renderer)) {
			warnings.push(
				"macOS platform with non-Apple/AMD WebGL renderer is inconsistent",
			);
		}
	}

	if (
		config.hardwareConcurrency < 1 ||
		config.hardwareConcurrency > 128 ||
		(config.hardwareConcurrency & (config.hardwareConcurrency - 1)) !== 0
	) {
		warnings.push("hardwareConcurrency should be a power of 2 between 1-128");
	}

	if (!VALID_DEVICE_MEMORY.includes(config.deviceMemory)) {
		warnings.push(
			`deviceMemory should be one of: ${VALID_DEVICE_MEMORY.join(", ")}`,
		);
	}

	if (config.maxTouchPoints > 0 && config.platform !== "linux") {
		if (
			config.platform === "windows" &&
			!config.userAgent.includes("Touch")
		) {
			warnings.push(
				"maxTouchPoints > 0 on non-touch Windows UA is inconsistent",
			);
		}
	}

	return warnings;
}
```

- [ ] **Step 2: Create fingerprint-preload.ts**

Create `apps/desktop/src/main/profile/fingerprint-preload.ts`:

```ts
import fs from "node:fs";
import path from "node:path";
import { app } from "electron";
import type { Fingerprint } from "../../stores/profile-store";

const PRELOAD_TEMPLATE = `
(function() {
	const fp = __PANE_FP_CONFIG__;

	const platformMap = { windows: "Win32", macos: "MacIntel", linux: "Linux x86_64" };
	const navPlatform = platformMap[fp.platform] || fp.platform;

	Object.defineProperty(navigator, "platform", { get: () => navPlatform });
	Object.defineProperty(navigator, "hardwareConcurrency", { get: () => fp.hardwareConcurrency });
	Object.defineProperty(navigator, "deviceMemory", { get: () => fp.deviceMemory });
	Object.defineProperty(navigator, "maxTouchPoints", { get: () => fp.maxTouchPoints });
	Object.defineProperty(navigator, "language", { get: () => fp.language });
	Object.defineProperty(navigator, "languages", { get: () => Object.freeze([...fp.languages]) });

	if (fp.screen) {
		Object.defineProperty(screen, "width", { get: () => fp.screen.width });
		Object.defineProperty(screen, "height", { get: () => fp.screen.height });
		Object.defineProperty(screen, "availWidth", { get: () => fp.screen.width });
		Object.defineProperty(screen, "availHeight", { get: () => fp.screen.height });
		if (fp.screen.colorDepth) {
			Object.defineProperty(screen, "colorDepth", { get: () => fp.screen.colorDepth });
			Object.defineProperty(screen, "pixelDepth", { get: () => fp.screen.colorDepth });
		}
	}

	if (fp.webgl) {
		const originalGetParameter = WebGLRenderingContext.prototype.getParameter;
		const UNMASKED_VENDOR = 0x9245;
		const UNMASKED_RENDERER = 0x9246;

		WebGLRenderingContext.prototype.getParameter = function(param) {
			if (param === UNMASKED_VENDOR) return fp.webgl.vendor;
			if (param === UNMASKED_RENDERER) return fp.webgl.renderer;
			return originalGetParameter.call(this, param);
		};

		if (typeof WebGL2RenderingContext !== "undefined") {
			const originalGetParameter2 = WebGL2RenderingContext.prototype.getParameter;

			WebGL2RenderingContext.prototype.getParameter = function(param) {
				if (param === UNMASKED_VENDOR) return fp.webgl.vendor;
				if (param === UNMASKED_RENDERER) return fp.webgl.renderer;
				return originalGetParameter2.call(this, param);
			};
		}
	}

	if (fp.canvas && fp.canvas.noise) {
		const seed = fp._profileHash || 0;
		function seededRandom(s) {
			s = (s * 1664525 + 1013904223) & 0xFFFFFFFF;
			return { next: s, value: (s >>> 16) / 65536 };
		}

		const originalToDataURL = HTMLCanvasElement.prototype.toDataURL;
		HTMLCanvasElement.prototype.toDataURL = function(...args) {
			const ctx = this.getContext("2d");
			if (ctx) {
				const imageData = ctx.getImageData(0, 0, this.width, this.height);
				let s = seed;
				for (let i = 0; i < imageData.data.length; i += 4) {
					const r = seededRandom(s + i);
					s = r.next;
					imageData.data[i] = (imageData.data[i] + (r.value < 0.5 ? -1 : 1)) & 0xFF;
				}
				ctx.putImageData(imageData, 0, 0);
			}
			return originalToDataURL.apply(this, args);
		};
	}

	if (fp.audio && fp.audio.noise) {
		const originalStartRendering = OfflineAudioContext.prototype.startRendering;

		OfflineAudioContext.prototype.startRendering = function() {
			return originalStartRendering.call(this).then(function(buffer) {
				const channel = buffer.getChannelData(0);
				for (let i = 0; i < channel.length; i++) {
					channel[i] += (Math.random() - 0.5) * 0.0001;
				}
				return buffer;
			});
		};
	}
})();
`;

export function generateFingerprintPreload(
	profileId: string,
	fingerprint: Fingerprint,
): string {
	const config = {
		...fingerprint,
		_profileHash: hashCode(profileId),
	};

	const content = PRELOAD_TEMPLATE.replace(
		"__PANE_FP_CONFIG__",
		JSON.stringify(config),
	);

	const tmpDir = path.join(app.getPath("temp"), "pane-fingerprints");

	if (!fs.existsSync(tmpDir)) {
		fs.mkdirSync(tmpDir, { recursive: true });
	}

	const filePath = path.join(tmpDir, `fp-${profileId}.js`);
	fs.writeFileSync(filePath, content, "utf-8");

	return filePath;
}

export function cleanupFingerprintPreload(profileId: string): void {
	const filePath = path.join(
		app.getPath("temp"),
		"pane-fingerprints",
		`fp-${profileId}.js`,
	);

	try {
		fs.unlinkSync(filePath);
	} catch {}
}

function hashCode(str: string): number {
	let hash = 0;

	for (let i = 0; i < str.length; i++) {
		hash = (hash * 31 + str.charCodeAt(i)) | 0;
	}

	return Math.abs(hash);
}
```

- [ ] **Step 3: Wire up fingerprint preload in Profile constructor**

In `apps/desktop/src/main/profile/profile.ts`, add the import at the top:

```ts
import {
	cleanupFingerprintPreload,
	generateFingerprintPreload,
} from "./fingerprint-preload";
```

Then in the constructor, after `this.session = session.fromPartition(...)` and before `ElectronChromeExtensions.handleCRXProtocol(...)`, add:

```ts
const profileData = profileStore
	.getState()
	.profiles.find((p) => p.id === id);

if (profileData?.fingerprint) {
	const fpPreloadPath = generateFingerprintPreload(id, profileData.fingerprint);
	this.session.registerPreloadScript({
		type: "frame",
		filePath: fpPreloadPath,
	});

	if (profileData.fingerprint.timezone) {
		const offset = getTimezoneOffsetMinutes(profileData.fingerprint.timezone);

		if (offset !== null) {
			this.session.setTimezoneOffset?.(offset);
		}
	}
}
```

Add the helper function at the bottom of the file:

```ts
function getTimezoneOffsetMinutes(timezone: string): number | null {
	try {
		const now = new Date();
		const utcDate = new Date(
			now.toLocaleString("en-US", { timeZone: "UTC" }),
		);
		const tzDate = new Date(
			now.toLocaleString("en-US", { timeZone: timezone }),
		);

		return (utcDate.getTime() - tzDate.getTime()) / 60000;
	} catch {
		return null;
	}
}
```

In the `destroy()` method, add cleanup:

```ts
destroy(): void {
	cleanupFingerprintPreload(this.id);
	this.tabs.closeAll();
	this.ece.destroy();
	extensionStore.getState().clearProfile(this.id);
}
```

- [ ] **Step 4: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors (note: `session.setTimezoneOffset` may not exist in type defs yet — the optional chain `?.` handles this)

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/main/profile/fingerprint-preload.ts apps/desktop/src/main/profile/fingerprint-validator.ts apps/desktop/src/main/profile/profile.ts
git commit -m "feat: add JS fingerprint preload engine with navigator, screen, WebGL, canvas, and audio spoofing"
```

---

### Task 14: Extend Fingerprint interface and defaults

**Files:**
- Modify: `apps/desktop/src/stores/profile-store.ts`
- Modify: `apps/desktop/src/renderer/components/default-fingerprints.ts`

- [ ] **Step 1: Add new fields to Fingerprint interface**

In `apps/desktop/src/stores/profile-store.ts`, update the `Fingerprint` interface. Replace:

```ts
screen: { width: number; height: number };
```

with:

```ts
screen: { width: number; height: number; colorDepth: number };
```

And add after `maxTouchPoints`:

```ts
canvas: { noise: boolean };
audio: { noise: boolean };
```

- [ ] **Step 2: Update default fingerprints**

In `apps/desktop/src/renderer/components/default-fingerprints.ts`, add the new fields to each template:

For `windows`:
```ts
screen: { width: 1920, height: 1080, colorDepth: 24 },
// ... after maxTouchPoints:
canvas: { noise: true },
audio: { noise: true },
```

For `macos`:
```ts
screen: { width: 1440, height: 900, colorDepth: 30 },
// ... after maxTouchPoints:
canvas: { noise: true },
audio: { noise: true },
```

For `linux`:
```ts
screen: { width: 1920, height: 1080, colorDepth: 24 },
// ... after maxTouchPoints:
canvas: { noise: true },
audio: { noise: true },
```

- [ ] **Step 3: Update profile-store merge to handle missing new fields**

In `apps/desktop/src/stores/profile-store.ts`, in the `merge` function, ensure backward compatibility for profiles persisted without the new fields. Update the merge callback to default new fields:

Inside the `merge` callback, after `isExpanded: false`, add fingerprint field defaults:

```ts
profiles: ((persisted as Partial<ProfileState>)?.profiles ?? []).map(
	(p) => ({
		...p,
		color: p.color ?? ProfileColor.BLUE,
		tabs: p.tabs.map((t) => ({ ...t, favicon: t.favicon ?? "" })),
		isExpanded: false,
		fingerprint: {
			...p.fingerprint,
			screen: {
				...p.fingerprint.screen,
				colorDepth: p.fingerprint.screen.colorDepth ?? 24,
			},
			canvas: p.fingerprint.canvas ?? { noise: true },
			audio: p.fingerprint.audio ?? { noise: true },
		},
	}),
),
```

- [ ] **Step 4: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts apps/desktop/src/renderer/components/default-fingerprints.ts
git commit -m "feat: extend Fingerprint interface with colorDepth, canvas noise, and audio noise"
```

---

## SP4: Performance

### Task 11: Set proxy once per session

**Files:**
- Modify: `apps/desktop/src/main/profile/profile.ts`
- Modify: `apps/desktop/src/main/profile/profile-tabs.ts`

- [ ] **Step 1: Move proxy setup from ProfileTabs.createView to Profile constructor**

In `apps/desktop/src/main/profile/profile.ts`, add proxy setup in the constructor after `this.session = session.fromPartition(...)`:

```ts
const profileData = profileStore
	.getState()
	.profiles.find((p) => p.id === id);
```

Note: if Task 10 already added this lookup, reuse it. Add after that lookup:

```ts
if (profileData?.proxy) {
	const p = profileData.proxy;

	this.session
		.setProxy({ proxyRules: `${p.proxyType}://${p.host}:${p.port}` })
		.catch((err) => {
			console.error(`[Profile ${id}] Proxy failed to apply:`, err);
		});
}
```

- [ ] **Step 2: Remove proxy setup from ProfileTabs.createView**

In `apps/desktop/src/main/profile/profile-tabs.ts`, remove the entire proxy block from `createView()`:

```ts
if (profile.proxy) {
	const p = profile.proxy;

	session
		.fromPartition(partition)
		.setProxy({ proxyRules: `${p.proxyType}://${p.host}:${p.port}` })
		.catch((err) => {
			console.error(
				`[Profile ${this.profile.id}] Proxy failed to apply:`,
				err,
			);
		});
}
```

Also remove the `session` import from `profile-tabs.ts` if no longer used elsewhere — check if `session` is still used. It is NOT used elsewhere in that file after removing the proxy block, so remove `session` from the import: change `import { type BaseWindow, session, type WebContents, WebContentsView }` to `import { type BaseWindow, type WebContents, WebContentsView }`.

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/profile/profile.ts apps/desktop/src/main/profile/profile-tabs.ts
git commit -m "perf: set proxy once per session in Profile constructor instead of per tab"
```

---

### Task 12: Add tabId → profileId index

**Files:**
- Modify: `apps/desktop/src/main/pane.ts`
- Modify: `apps/desktop/src/main/ipc.ts`

- [ ] **Step 1: Add tab index to Pane**

In `apps/desktop/src/main/pane.ts`, add:

```ts
private readonly tabIndex = new Map<string, string>();
```

Add methods:

```ts
registerTab(tabId: string, profileId: string): void {
	this.tabIndex.set(tabId, profileId);
}

unregisterTab(tabId: string): void {
	this.tabIndex.delete(tabId);
}

getProfileForTab(tabId: string): Profile | undefined {
	const profileId = this.tabIndex.get(tabId);

	return profileId ? this.profiles.get(profileId) : undefined;
}
```

- [ ] **Step 2: Use tab index in IpcRouter**

In `apps/desktop/src/main/ipc.ts`, replace the `tabs:close` handler:

```ts
this.handle("tabs:close", (_e, tabId: string) => {
	const profile = this.pane.getProfileForTab(tabId);

	if (profile) {
		profile.tabs.close(tabId);
	}
});
```

Replace the `tabs:switch` handler:

```ts
this.handle("tabs:switch", (_e, tabId: string) => {
	this.pane.hideAllTabs();
	const profile = this.pane.getProfileForTab(tabId);

	if (profile) {
		profile.tabs.activate(tabId);
	}
});
```

- [ ] **Step 3: Register/unregister tabs in ProfileTabs**

In `apps/desktop/src/main/profile/profile-tabs.ts`, update the `TabHost` interface to include the registration methods. Add to the interface:

```ts
registerTab(tabId: string): void;
unregisterTab(tabId: string): void;
```

In `open()`, after `this.views.set(id, view)`:

```ts
this.profile.registerTab(id);
```

In `close()`, after `this.views.delete(tabId)`:

```ts
this.profile.unregisterTab(tabId);
```

In `openForExtension()`, after `this.views.set(tabId, view)`:

```ts
this.profile.registerTab(tabId);
```

Then in `apps/desktop/src/main/profile/profile.ts`, implement the interface methods by delegating to `Pane`. Since `Profile` doesn't have a direct reference to `Pane`, pass a callback instead. Add to the `Profile` constructor params a `registerTab` and `unregisterTab` callback, or simpler: since `Profile` doesn't know about `Pane`, add the methods directly:

Actually, the simpler approach: have `ProfileTabs` call its host's methods. Update `Profile` to implement the `registerTab`/`unregisterTab` by storing the pane reference. But `Profile` doesn't have a `Pane` reference. The cleanest approach: pass the tab index map to Profile.

Simplest approach: pass the tab index functions through when creating Profile in `Pane`:

In `apps/desktop/src/main/pane.ts`, update `Profile` creation in `createProfile`, `getOrCreateProfile`, and `restore` to pass `this.registerTab.bind(this)` and `this.unregisterTab.bind(this)`:

Actually this is getting complicated. Let's keep it simple — just call `pane.registerTab` from `IpcRouter` after the tab operations, since the IPC router already orchestrates both. 

Alternatively, the simplest fix: update `Pane.getProfileForTab` to fall back to linear scan if not in index, and populate the index opportunistically. This way the index is an optimization, not a requirement.

Let me simplify. Just add the `tabIndex` to `Pane` and populate it from `IpcRouter` handlers:

In the `tabs:open` handler:

```ts
this.handle("tabs:open", (_e, profileId: string, url?: string) => {
	this.pane.hideAllTabs();
	const view = this.pane.getOrCreateProfile(profileId).tabs.open(url);
	// Tab ID is the last opened tab
	const activeTabId = tabStore.getState().activeTabId;
	if (activeTabId) {
		this.pane.registerTab(activeTabId, profileId);
	}
});
```

Actually this is still messy. Let me use the simplest approach: `ProfileTabs.open` returns the tabId, and the IPC router registers it.

Revise — keep it truly simple:

In `apps/desktop/src/main/profile/profile-tabs.ts`, make `open()` return the tab ID:

The method already creates a `const id = tabId ?? crypto.randomUUID()`. It returns a `WebContentsView`. Let's have it also register with the index via a callback on the host interface.

OK, let me just use the simplest viable pattern:

- [ ] **Revised Step 2-3: Simple tab index via ProfileTabs host**

Add to the `TabHost` interface in `profile-tabs.ts`:

```ts
onTabOpened(tabId: string): void;
onTabClosed(tabId: string): void;
```

In `ProfileTabs.open()`, after `this.views.set(id, view)`, add:

```ts
this.profile.onTabOpened(id);
```

In `ProfileTabs.close()`, after `this.views.delete(tabId)`, add:

```ts
this.profile.onTabClosed(tabId);
```

In `ProfileTabs.openForExtension()`, after `this.views.set(tabId, view)`, add:

```ts
this.profile.onTabOpened(tabId);
```

In `Profile`, add a constructor parameter for the pane's register/unregister:

```ts
constructor(
	readonly id: string,
	private readonly mainWindow: BaseWindow,
	extensionsPath: string,
	private readonly onTabOpened?: (tabId: string, profileId: string) => void,
	private readonly onTabClosed?: (tabId: string) => void,
) {
```

Implement the TabHost methods:

```ts
onTabOpened(tabId: string): void {
	this._onTabOpened?.(tabId, this.id);
}

onTabClosed(tabId: string): void {
	this._onTabClosed?.(tabId);
}
```

Wait, naming conflict. Let me rename the constructor params:

```ts
constructor(
	readonly id: string,
	private readonly mainWindow: BaseWindow,
	extensionsPath: string,
	private readonly tabRegistered?: (tabId: string, profileId: string) => void,
	private readonly tabUnregistered?: (tabId: string) => void,
) {
```

Then:

```ts
onTabOpened(tabId: string): void {
	this.tabRegistered?.(tabId, this.id);
}

onTabClosed(tabId: string): void {
	this.tabUnregistered?.(tabId);
}
```

In `Pane`, update all Profile creations to pass the callbacks:

```ts
new Profile(
	id,
	this.mainWindow,
	this.extensionsPath,
	(tabId, profileId) => this.tabIndex.set(tabId, profileId),
	(tabId) => this.tabIndex.delete(tabId),
)
```

- [ ] **Step 4: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/main/pane.ts apps/desktop/src/main/ipc.ts apps/desktop/src/main/profile/profile.ts apps/desktop/src/main/profile/profile-tabs.ts
git commit -m "perf: add tabId→profileId index for O(1) tab lookups"
```

---

### Task 13: Make getInstalled() async

**Files:**
- Modify: `apps/desktop/src/main/extension-installer.ts:58-151`

- [ ] **Step 1: Convert to async fs operations**

In `apps/desktop/src/main/extension-installer.ts`, replace the `fs` import:

```ts
import fs from "node:fs";
```

with:

```ts
import fs from "node:fs/promises";
```

Replace the entire `getInstalled()` method with an async version:

```ts
async getInstalled(): Promise<InstalledExtension[]> {
	const result: InstalledExtension[] = [];

	try {
		await fs.access(this.extensionsPath);
	} catch {
		return result;
	}

	const extIds = await fs.readdir(this.extensionsPath);

	for (const extId of extIds) {
		const extDir = path.join(this.extensionsPath, extId);
		const stat = await fs.stat(extDir);

		if (!stat.isDirectory()) {
			continue;
		}

		const versions = await fs.readdir(extDir);

		for (const version of versions) {
			const manifestPath = path.join(extDir, version, "manifest.json");

			try {
				const manifestRaw = await fs.readFile(manifestPath, "utf-8");
				const manifest = JSON.parse(manifestRaw);
				let name: string = manifest.name ?? extId;

				if (name.startsWith("__MSG_") && name.endsWith("__")) {
					const msgKey = name.slice(6, -2);

					try {
						const messagesPath = path.join(
							extDir,
							version,
							"_locales",
							"en",
							"messages.json",
						);

						const messages = JSON.parse(
							await fs.readFile(messagesPath, "utf-8"),
						);

						name = messages[msgKey]?.message ?? name;
					} catch {}
				}

				let description: string = manifest.description ?? "";

				if (description.startsWith("__MSG_") && description.endsWith("__")) {
					const msgKey = description.slice(6, -2);

					try {
						const messagesPath = path.join(
							extDir,
							version,
							"_locales",
							"en",
							"messages.json",
						);

						const messages = JSON.parse(
							await fs.readFile(messagesPath, "utf-8"),
						);

						description = messages[msgKey]?.message ?? description;
					} catch {}
				}

				const icons: Record<string, string> | undefined = manifest.icons;
				let icon = "";

				if (icons) {
					const largest = Object.keys(icons)
						.map(Number)
						.sort((a, b) => b - a)[0];

					if (largest) {
						icon = `pane-extension://${extId}/icon`;
					}
				}

				result.push({
					id: extId,
					name,
					version: manifest.version ?? version,
					description,
					icon,
				});
			} catch (err) {
				console.warn(`[CWS] Skipping extension ${extId}/${version}:`, err);
			}
		}
	}

	return result;
}
```

Also update `Pane.restore()` in `pane.ts` where `getInstalled()` is called synchronously. Change:

```ts
const installed = this.extensions.getInstalled();
```

to:

```ts
const installed = await this.extensions.getInstalled();
```

And make the dev extension block async. Wrap the block:

```ts
if (process.env.NODE_ENV !== "production") {
	this.extensions.getInstalled().then((installed) => {
		const installedIds = new Set(installed.map((e) => e.id));
		// ... rest of dev extension logic
	});
}
```

Actually simpler — since `restore()` is called from `setup()` which doesn't await, just make `restore()` async and leave the call site as-is (fire and forget):

Change `restore(): void` to `async restore(): Promise<void>` and update the dev block accordingly.

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extension-installer.ts apps/desktop/src/main/pane.ts
git commit -m "perf: make getInstalled() async to unblock main event loop"
```

---

### Task 15: Lazy tab restoration

**Files:**
- Modify: `apps/desktop/src/main/pane.ts`
- Modify: `apps/desktop/src/stores/profile-store.ts`

- [ ] **Step 1: Add isLoaded field to Tab interface**

In `apps/desktop/src/stores/profile-store.ts`, add `isLoaded` to the `Tab` interface:

```ts
export interface Tab {
	id: string;
	url: string;
	title: string;
	favicon: string;
	isLoaded: boolean;
}
```

Update `openTab` to set `isLoaded: true`:

```ts
tabs: [
	...p.tabs,
	{ id: tabId, url, title: "New Tab", favicon: "", isLoaded: true },
],
```

Update `partialize` to exclude `isLoaded`:

```ts
partialize: (state) => ({
	profiles: state.profiles.map(({ isExpanded, ...profile }) => ({
		...profile,
		tabs: profile.tabs.map(({ isLoaded, ...tab }) => tab),
	})),
}),
```

Update `merge` to set `isLoaded: false` for restored tabs:

```ts
tabs: p.tabs.map((t) => ({ ...t, favicon: t.favicon ?? "", isLoaded: false })),
```

- [ ] **Step 2: Update Pane.restore to not create WebContentsViews for saved tabs**

In `apps/desktop/src/main/pane.ts`, the `restore()` method currently creates `Profile` instances for every saved profile but doesn't open tabs (tabs are just metadata in the store). This is already lazy — the `Profile` constructor doesn't auto-open tabs. The key change is in `ProfileTabs.activate()` — it already has lazy loading logic (lines 77-99 in profile-tabs.ts) that creates a view on demand if `this.views.get(tabId)` returns undefined.

The current `activate()` method already does this correctly:

```ts
if (!view) {
	const tab = this.profile.data.tabs.find((t) => t.id === tabId);
	if (tab) {
		view = this.createView(tabId);
		this.views.set(tabId, view);
		view.webContents.loadURL(tab.url);
	}
}
```

So lazy restoration already works at the WebContentsView level. Just ensure `isLoaded` is updated when a tab is actually loaded. Add to `ProfileTabs.activate()`, after the view is created and loaded:

In the `if (!view)` block, after `view.webContents.loadURL(tab.url)`, add:

```ts
profileStore.getState().updateTab(tabId, { isLoaded: true } as Partial<import("../../stores/profile-store").Tab>);
```

And in `ProfileTabs.open()`, the tab is already created with `isLoaded: true` in the store.

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts apps/desktop/src/main/profile/profile-tabs.ts
git commit -m "feat: lazy tab restoration — only create WebContentsView when tab is activated"
```

---

### Task 16: Destroy WebContentsView on window close

**Files:**
- Modify: `apps/desktop/src/main/profile/profile-tabs.ts`
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Add destroyAll method to ProfileTabs**

In `apps/desktop/src/main/profile/profile-tabs.ts`, add:

```ts
destroyAll(): void {
	for (const view of this.views.values()) {
		this.mainWindow.contentView.removeChildView(view);
		view.webContents.close();
	}

	this.views.clear();
}
```

- [ ] **Step 2: Call destroyAll on window close**

In `apps/desktop/src/main/index.ts`, update the `closed` handler (which already has `ipcRouter.cleanup()` from Task 6):

```ts
win.mainWindow.on("closed", () => {
	if (pane) {
		for (const profile of pane.profiles.values()) {
			profile.tabs.destroyAll();
		}
	}

	ipcRouter.cleanup();
	win = null;
	pane = null;
});
```

- [ ] **Step 3: Expose destroyAll from Profile**

In `apps/desktop/src/main/profile/profile.ts`, the `destroy()` method already calls `this.tabs.closeAll()`. But `closeAll` does profile store cleanup. For window close, we just want to free memory without store updates. The `destroyAll()` method on `ProfileTabs` handles this correctly (it doesn't update stores).

No changes needed to `profile.ts` — we call `profile.tabs.destroyAll()` directly from `index.ts`.

- [ ] **Step 4: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/main/profile/profile-tabs.ts apps/desktop/src/main/index.ts
git commit -m "fix: destroy WebContentsView webContents on window close to prevent memory leak"
```

---

### Task 17: Flush fs-storage debounce on quit

**Files:**
- Modify: `apps/desktop/src/stores/middlewares/fs-storage.ts`
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Store pending values and add flush function**

In `apps/desktop/src/stores/middlewares/fs-storage.ts`, change the debounce timer map to include the pending value. Replace:

```ts
const debounceTimers = new Map<string, ReturnType<typeof setTimeout>>();
```

with:

```ts
const pendingWrites = new Map<
	string,
	{ timer: ReturnType<typeof setTimeout>; value: string }
>();
```

Update `setItem` to use `pendingWrites`:

```ts
setItem(name: string, value: string): void {
	if (typeof window !== "undefined") {
		return;
	}

	const existing = pendingWrites.get(name);

	if (existing) {
		clearTimeout(existing.timer);
	}

	const timer = setTimeout(() => {
		pendingWrites.delete(name);
		const filePath = resolvePath(name);
		const tmpPath = `${filePath}.tmp`;
		const fs = require("node:fs");
		fs.writeFileSync(tmpPath, value, "utf-8");
		fs.renameSync(tmpPath, filePath);
	}, DEBOUNCE_MS);

	pendingWrites.set(name, { timer, value });
},
```

Update `removeItem` — no changes needed since it doesn't use debounce timers.

Add the exported flush function:

```ts
export function flushPendingWrites(): void {
	for (const [name, { timer, value }] of pendingWrites) {
		clearTimeout(timer);
		const filePath = resolvePath(name);
		const tmpPath = `${filePath}.tmp`;
		const fs = require("node:fs");
		fs.writeFileSync(tmpPath, value, "utf-8");
		fs.renameSync(tmpPath, filePath);
	}

	pendingWrites.clear();
}
```

- [ ] **Step 2: Call flush on before-quit**

In `apps/desktop/src/main/index.ts`, add the import:

```ts
import { flushPendingWrites } from "../stores/middlewares/fs-storage";
```

Add before `app.on("window-all-closed", ...)`:

```ts
app.on("before-quit", () => {
	flushPendingWrites();
});
```

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/stores/middlewares/fs-storage.ts apps/desktop/src/main/index.ts
git commit -m "fix: flush pending fs-storage writes on app quit"
```

---

## SP5: DX & Modernization

### Task 19: Extract layout constants

**Files:**
- Create: `apps/desktop/src/constants/layout.ts`
- Modify: `apps/desktop/src/main/profile/profile-tabs.ts:11-14`

- [ ] **Step 1: Create layout constants file**

Create `apps/desktop/src/constants/layout.ts`:

```ts
export const SIDEBAR_WIDTH = 220;
export const TOOLBAR_HEIGHT = 51;
export const PANEL_MARGIN_RIGHT = 8;
export const PANEL_MARGIN_BOTTOM = 8;
```

- [ ] **Step 2: Import constants in profile-tabs.ts**

In `apps/desktop/src/main/profile/profile-tabs.ts`, remove the local constants:

```ts
const SIDEBAR_WIDTH = 220;
const TOOLBAR_HEIGHT = 51;
const PANEL_MARGIN_RIGHT = 8;
const PANEL_MARGIN_BOTTOM = 8;
```

Add the import:

```ts
import {
	PANEL_MARGIN_BOTTOM,
	PANEL_MARGIN_RIGHT,
	SIDEBAR_WIDTH,
	TOOLBAR_HEIGHT,
} from "../../constants/layout";
```

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/constants/layout.ts apps/desktop/src/main/profile/profile-tabs.ts
git commit -m "refactor: extract layout constants to shared constants file"
```

---

### Task 20: Sort extension version directories

**Files:**
- Modify: `apps/desktop/src/main/profile/extension-loader.ts:53-59`
- Modify: `apps/desktop/src/main/index.ts:83`
- Modify: `apps/desktop/src/main/extension-installer.ts` (if applicable after Task 13 changes)

- [ ] **Step 1: Add version sort utility**

In `apps/desktop/src/main/profile/extension-loader.ts`, add a function before the class:

```ts
function latestVersion(versions: string[]): string | undefined {
	return versions
		.sort((a, b) => {
			const pa = a.split(".").map(Number);
			const pb = b.split(".").map(Number);

			for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
				const diff = (pa[i] ?? 0) - (pb[i] ?? 0);

				if (diff !== 0) return diff;
			}

			return 0;
		})
		.at(-1);
}
```

Replace `versions[0]` in `loadOne()`:

```ts
const extPath = path.join(extDir, versions[0]);
```

with:

```ts
const latest = latestVersion(versions);

if (!latest) {
	return;
}

const extPath = path.join(extDir, latest);
```

- [ ] **Step 2: Fix version[0] in index.ts protocol handler**

In `apps/desktop/src/main/index.ts`, in the `pane-extension` protocol handler, replace:

```ts
const versionDir = path.join(extDir, versions[0]);
```

with:

```ts
const sorted = versions.sort((a, b) => {
	const pa = a.split(".").map(Number);
	const pb = b.split(".").map(Number);

	for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
		const diff = (pa[i] ?? 0) - (pb[i] ?? 0);

		if (diff !== 0) return diff;
	}

	return 0;
});

const versionDir = path.join(extDir, sorted[sorted.length - 1]);
```

- [ ] **Step 3: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/profile/extension-loader.ts apps/desktop/src/main/index.ts
git commit -m "fix: sort extension version directories to always pick the latest"
```

---

### Task 21: Single demux listener for sync

**Files:**
- Modify: `apps/desktop/src/stores/middlewares/sync.ts`

- [ ] **Step 1: Replace per-store listeners with single demux**

Replace the entire `apps/desktop/src/stores/middlewares/sync.ts`:

```ts
import type { StateCreator } from "zustand/vanilla";
import { serializeState } from "./serialize";

export interface SyncConfig {
	name: string;
}

function isRenderer(): boolean {
	return typeof window !== "undefined" && "electronSync" in window;
}

const listeners = new Map<string, (state: string) => void>();
let listenerRegistered = false;

function ensureListener(): void {
	if (listenerRegistered) return;

	listenerRegistered = true;

	window.electronSync.onReceive((storeName, state) => {
		listeners.get(storeName)?.(state);
	});
}

export function sync<TState>(
	storeCreator: StateCreator<TState, [], []>,
	config: SyncConfig,
): StateCreator<TState, [], []> {
	const { name } = config;

	return (set, get, api) => {
		if (!isRenderer()) {
			return storeCreator(set, get, api);
		}

		let applyingRemoteDepth = 0;

		const syncedSet: typeof set = (updater, replace) => {
			set(updater, replace as never);

			if (applyingRemoteDepth > 0) {
				return;
			}

			try {
				window.electronSync.send(name, serializeState(get()));
			} catch {}
		};

		const applyRemoteState = (serialized: string) => {
			try {
				const partial = JSON.parse(serialized) as Partial<TState>;
				applyingRemoteDepth++;
				set((state) => ({ ...state, ...partial }));
				applyingRemoteDepth--;
			} catch {
				applyingRemoteDepth--;
			}
		};

		listeners.set(name, applyRemoteState);
		ensureListener();

		window.electronSync
			.requestState(name)
			.then((state) => {
				if (state) {
					applyRemoteState(state);
				}
			})
			.catch(() => {});

		return storeCreator(syncedSet, get, api);
	};
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/middlewares/sync.ts
git commit -m "perf: single demux listener for store sync instead of one per store"
```

---

### Task 22: Granular sidebar selectors

**Files:**
- Modify: `apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx`

- [ ] **Step 1: Split into per-profile components with granular selectors**

Replace the entire `apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx`:

```ts
import { Settings, Trash2, X } from "lucide-react";
import { useState } from "react";
import { toast } from "sonner";
import { useStore } from "zustand/react";
import { useShallow } from "zustand/react/shallow";

import { navigationStore, Page } from "../../../stores/navigation-store";
import { profileStore } from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import { CreateProfileSheet } from "../create-profile-sheet";
import {
	ProfileBadge,
	ProfileHeader,
	ProfileItem,
	ProfileName,
	ProfileTabs,
} from "./profile-item";
import {
	Sidebar,
	SidebarContent,
	SidebarFooter,
	SidebarHeader,
	SidebarNewButton,
	SidebarSeparator,
	SidebarSettingsButton,
	SidebarTitle,
} from "./sidebar";
import { TabFavicon, TabItem, TabNew, TabTitle } from "./tab-item";

function ConnectedProfileItem({ id }: { id: string }) {
	const profile = useStore(
		profileStore,
		(s) => s.profiles.find((p) => p.id === id),
	);
	const activeTabId = useStore(tabStore, (s) => s.activeTabId);
	const page = useStore(navigationStore, (s) => s.page);

	if (!profile) return null;

	const isRunning = profile.tabs.length > 0;

	return (
		<ProfileItem>
			<ProfileHeader
				color={profile.color}
				active={isRunning}
				onClick={() => {
					if (!profile.isExpanded) {
						window.pane.profiles.activate(profile.id);
					}

					profileStore.getState().toggleExpanded(profile.id);
				}}
			>
				<ProfileName>{profile.name}</ProfileName>
				{!isRunning ? (
					<Trash2
						className="h-3.5 w-3.5 shrink-0 text-muted-foreground opacity-0 transition-opacity group-hover:opacity-100"
						onClick={(e) => {
							e.stopPropagation();
							profileStore.getState().remove(profile.id);
							toast.success("Profile deleted");
						}}
					/>
				) : null}
				{isRunning && !profile.isExpanded ? (
					<ProfileBadge>{profile.tabs.length}</ProfileBadge>
				) : null}
			</ProfileHeader>

			{profile.isExpanded ? (
				<ProfileTabs>
					{profile.tabs.map((tab) => (
						<TabItem
							key={tab.id}
							active={activeTabId === tab.id && page === Page.BROWSER}
							onClick={() => {
								navigationStore.getState().navigate(Page.BROWSER);
								window.pane.tabs.switch(tab.id);
							}}
						>
							<TabFavicon src={tab.favicon || undefined} />
							<TabTitle>{tab.title || "Loading..."}</TabTitle>
							<X
								className="h-3 w-3 shrink-0 opacity-0 transition-opacity group-hover:opacity-100"
								onClick={(e) => {
									e.stopPropagation();
									window.pane.tabs.close(tab.id);
								}}
							/>
						</TabItem>
					))}
					<TabNew
						onClick={() => {
							navigationStore.getState().navigate(Page.BROWSER);
							window.pane.tabs.open(profile.id);
						}}
					/>
				</ProfileTabs>
			) : null}
		</ProfileItem>
	);
}

export function SidebarConnected() {
	const profileIds = useStore(
		profileStore,
		useShallow((s) => s.profiles.map((p) => p.id)),
	);
	const page = useStore(navigationStore, (s) => s.page);
	const [sheetOpen, setSheetOpen] = useState(false);

	return (
		<Sidebar>
			<SidebarHeader>
				<SidebarTitle>Pane</SidebarTitle>
			</SidebarHeader>

			<SidebarContent>
				{profileIds.map((id) => (
					<ConnectedProfileItem key={id} id={id} />
				))}
			</SidebarContent>

			<SidebarFooter>
				<SidebarNewButton onClick={() => setSheetOpen(true)} />
				<SidebarSeparator />
				<SidebarSettingsButton
					active={page === Page.SETTINGS}
					onClick={() => navigationStore.getState().navigate(Page.SETTINGS)}
				>
					<Settings className="h-3.5 w-3.5" />
					Settings
				</SidebarSettingsButton>
			</SidebarFooter>

			<CreateProfileSheet open={sheetOpen} onOpenChange={setSheetOpen} />
		</Sidebar>
	);
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx
git commit -m "perf: granular sidebar selectors — per-profile subscriptions reduce re-renders"
```

---

### Task 23: Merge address bar useStore calls

**Files:**
- Modify: `apps/desktop/src/renderer/pages/browser/_components/address-bar-connected.tsx:20-21`

- [ ] **Step 1: Merge the two useStore calls**

In `apps/desktop/src/renderer/pages/browser/_components/address-bar-connected.tsx`, add the import:

```ts
import { useShallow } from "zustand/react/shallow";
```

Replace:

```ts
const activeTabId = useStore(tabStore, (s) => s.activeTabId);
const activeProfileId = useStore(tabStore, (s) => s.activeProfileId);
```

with:

```ts
const { activeTabId, activeProfileId } = useStore(
	tabStore,
	useShallow((s) => ({
		activeTabId: s.activeTabId,
		activeProfileId: s.activeProfileId,
	})),
);
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/pages/browser/_components/address-bar-connected.tsx
git commit -m "perf: merge two useStore subscriptions into one in address bar"
```

---

### Task 24: Targeted updateTab/closeTab

**Files:**
- Modify: `apps/desktop/src/stores/profile-store.ts:120-138`

- [ ] **Step 1: Optimize updateTab to preserve unaffected profile references**

In `apps/desktop/src/stores/profile-store.ts`, replace the `updateTab` method:

```ts
updateTab: (tabId, partial) => {
	set((s) => ({
		profiles: s.profiles.map((p) => ({
			...p,
			tabs: p.tabs.map((t) =>
				t.id === tabId ? { ...t, ...partial } : t,
			),
		})),
	}));
},
```

with:

```ts
updateTab: (tabId, partial) => {
	set((s) => ({
		profiles: s.profiles.map((p) => {
			if (!p.tabs.some((t) => t.id === tabId)) return p;

			return {
				...p,
				tabs: p.tabs.map((t) =>
					t.id === tabId ? { ...t, ...partial } : t,
				),
			};
		}),
	}));
},
```

Replace the `closeTab` method similarly:

```ts
closeTab: (tabId) => {
	set((s) => ({
		profiles: s.profiles.map((p) => ({
			...p,
			tabs: p.tabs.filter((t) => t.id !== tabId),
		})),
	}));
},
```

with:

```ts
closeTab: (tabId) => {
	set((s) => ({
		profiles: s.profiles.map((p) => {
			if (!p.tabs.some((t) => t.id === tabId)) return p;

			return {
				...p,
				tabs: p.tabs.filter((t) => t.id !== tabId),
			};
		}),
	}));
},
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts
git commit -m "perf: targeted updateTab/closeTab preserves unaffected profile references"
```

---

### Task 25: Update electron-vite config

**Files:**
- Modify: `apps/desktop/electron-vite.config.ts`

- [ ] **Step 1: Remove deprecated externalizeDepsPlugin**

Replace the entire `apps/desktop/electron-vite.config.ts`:

```ts
import react from "@vitejs/plugin-react";
import { defineConfig } from "electron-vite";

export default defineConfig({
	main: {},
	preload: {},
	renderer: {
		plugins: [react()],
	},
});
```

- [ ] **Step 2: Verify the build still works**

Run: `bun turbo run build --filter=@pane/desktop`
Expected: Build succeeds. If `externalizeDepsPlugin` is still required (electron-vite version < 5.0), revert and keep the plugin.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/electron-vite.config.ts
git commit -m "chore: remove deprecated externalizeDepsPlugin from electron-vite config"
```

---

### Task 26: ExtensionLoader retry cap

**Files:**
- Modify: `apps/desktop/src/main/profile/extension-loader.ts`

- [ ] **Step 1: Add retry counter**

In `apps/desktop/src/main/profile/extension-loader.ts`, add a retry counter field and cap:

```ts
export class ExtensionLoader {
	private loadPromise: Promise<void> | null = null;
	private retryCount = 0;
	private static readonly MAX_RETRIES = 3;
```

Replace the `ensureLoaded` method:

```ts
ensureLoaded(): Promise<void> {
	if (this.loadPromise) return this.loadPromise;

	if (this.retryCount >= ExtensionLoader.MAX_RETRIES) {
		return Promise.resolve();
	}

	this.loadPromise = this.load().catch((err) => {
		this.loadPromise = null;
		this.retryCount++;
		console.error(
			`[Profile] Extension load failed (attempt ${this.retryCount}/${ExtensionLoader.MAX_RETRIES}):`,
			err,
		);
	});

	return this.loadPromise;
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/profile/extension-loader.ts
git commit -m "fix: cap ExtensionLoader retries at 3 to prevent infinite retry loops"
```

---

### Task 28: chrome.scripting stub

**Files:**
- Modify: `packages/electron-chrome-extensions/src/browser/api/index.ts` (or wherever extension API shims are registered)

- [ ] **Step 1: Add chrome.scripting stub**

Search for where extension APIs are shimmed in the `@pane/electron-chrome-extensions` package. Add a `chrome.scripting` stub that prevents MV3 extensions from throwing when calling `chrome.scripting.executeScript`:

```ts
chrome.scripting = {
  executeScript: async () => [],
  insertCSS: async () => [],
  removeCSS: async () => [],
  registerContentScripts: async () => [],
  unregisterContentScripts: async () => [],
  getRegisteredContentScripts: async () => [],
  updateContentScripts: async () => [],
};
```

This is a no-op stub — extensions that use `chrome.scripting` will silently get empty results instead of throwing.

- [ ] **Step 2: Verify typecheck passes**

Run: `bun turbo run typecheck --filter=@pane/desktop`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add packages/electron-chrome-extensions/
git commit -m "feat: add chrome.scripting API stub for MV3 extension compatibility"
```

---

### Task 27: Dev extension auto-install guard

**Files:**
- Modify: `apps/desktop/src/main/pane.ts:66-85`

- [ ] **Step 1: Gate behind environment variable**

In `apps/desktop/src/main/pane.ts`, replace:

```ts
if (process.env.NODE_ENV !== "production") {
```

with:

```ts
if (process.env.PANE_DEV_EXTENSIONS === "1") {
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/pane.ts
git commit -m "chore: gate dev extension auto-install behind PANE_DEV_EXTENSIONS env var"
```
