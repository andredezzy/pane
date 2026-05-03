# Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve DX across the Pane monorepo by fixing 11 structural issues: dead code, wrong config defaults, missing data model fields, duplicated utilities, mixed concerns, and a god-method.

**Architecture:** Bottom-up execution in 4 phases. Phase 1 removes dead code and fixes configs. Phase 2 adds `activeProfileId` and deduplicates `serialize`. Phase 3 splits `profile-store` concerns, extracts IPC to `IpcRouter`, and moves hardcoded fingerprints. Phase 4 replaces `require()` with ESM imports. Tests are deferred to a follow-up plan.

**Tech Stack:** TypeScript, Zustand, Electron, React, electron-vite, Turborepo

---

## File Structure

**New files:**
- `apps/desktop/src/stores/serialize.ts` — shared `serializeState` utility
- `apps/desktop/src/stores/profile-colors.ts` — `ProfileColor` enum and `PROFILE_COLOR_HEX` map
- `apps/desktop/src/stores/default-fingerprints.ts` — `DEFAULT_FINGERPRINTS` per-platform data
- `apps/desktop/src/main/ipc.ts` — `IpcRouter` class

**Deleted files/dirs:**
- `packages/core/` (entire directory)
- `main.js` (root, empty)

**Modified files:**
- `packages/config-typescript/base.json`
- `packages/ui/tsconfig.json`
- `packages/config-tsdown/tsconfig.json`
- `apps/desktop/tsconfig.json`
- `knip.json`
- `apps/desktop/src/stores/tab-store.ts`
- `apps/desktop/src/main/profile-tabs.ts`
- `apps/desktop/src/main/store-sync.ts`
- `apps/desktop/src/stores/middlewares/sync.ts`
- `apps/desktop/src/stores/profile-store.ts`
- `apps/desktop/src/renderer/components/address-bar/address-bar.tsx`
- `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`
- `apps/desktop/src/renderer/components/color-picker.tsx`
- `apps/desktop/src/renderer/components/create-profile-sheet.tsx`
- `apps/desktop/src/renderer/components/sidebar/profile-item.tsx`
- `apps/desktop/src/main/pane.ts`
- `apps/desktop/src/main/index.ts`
- `apps/desktop/src/stores/middlewares/fs-storage.ts`

---

## Task 1: Fix `base.json` tsconfig defaults

**Files:**
- Modify: `packages/config-typescript/base.json:7-8`
- Modify: `packages/ui/tsconfig.json:6-7`
- Modify: `packages/config-tsdown/tsconfig.json:6-7`
- Modify: `apps/desktop/tsconfig.json:4-5`

- [ ] **Step 1: Update `base.json` to use bundler resolution**

In `packages/config-typescript/base.json`, change lines 7-8 from:

```json
"module": "NodeNext",
"moduleResolution": "NodeNext",
```

to:

```json
"module": "ESNext",
"moduleResolution": "bundler",
```

- [ ] **Step 2: Remove redundant overrides from `packages/ui/tsconfig.json`**

Remove the `"module": "ESNext"` and `"moduleResolution": "bundler"` lines from `compilerOptions`, leaving:

```json
{
	"extends": "@pane/typescript-config/react-library.json",
	"compilerOptions": {
		"rootDir": "./src",
		"outDir": "./dist",
		"composite": false
	},
	"include": ["src/**/*.ts", "src/**/*.tsx"],
	"exclude": ["node_modules", "dist"]
}
```

- [ ] **Step 3: Remove redundant overrides from `packages/config-tsdown/tsconfig.json`**

Remove the `"module": "ESNext"` and `"moduleResolution": "bundler"` lines from `compilerOptions`, leaving:

```json
{
	"$schema": "https://json.schemastore.org/tsconfig",
	"extends": "@pane/typescript-config/base.json",

	"compilerOptions": {
		"rootDir": "./src",
		"outDir": "./dist",
		"composite": false
	},

	"include": ["src/**/*.ts"],
	"exclude": ["node_modules", "dist"]
}
```

- [ ] **Step 4: Remove redundant overrides from `apps/desktop/tsconfig.json`**

Remove the `"module": "ESNext"` and `"moduleResolution": "bundler"` lines from `compilerOptions`, leaving:

```json
{
	"extends": "@pane/typescript-config/base.json",
	"compilerOptions": {
		"composite": false
	},
	"include": ["src/**/*.ts", "src/**/*.tsx"],
	"exclude": ["node_modules", "out", "release"]
}
```

- [ ] **Step 5: Verify typecheck and build**

Run:
```bash
bun turbo run typecheck && bun turbo run build
```
Expected: all packages pass (8/8 typecheck, 5/5 build — `@pane/core` still exists at this point).

- [ ] **Step 6: Commit**

```bash
git add packages/config-typescript/base.json packages/ui/tsconfig.json packages/config-tsdown/tsconfig.json apps/desktop/tsconfig.json
git commit -m "$(cat <<'EOF'
refactor: set bundler module resolution as the base tsconfig default

Every consumer was overriding NodeNext to bundler individually.
EOF
)"
```

---

## Task 2: Delete `@pane/core` and empty `main.js`

**Files:**
- Delete: `packages/core/` (entire directory)
- Delete: `main.js`
- Modify: `knip.json`

- [ ] **Step 1: Verify nothing imports `@pane/core`**

Run:
```bash
grep -rn '@pane/core' apps/ packages/ --include='*.ts' --include='*.tsx' --include='*.json' | grep -v 'packages/core/'
```
Expected: no output (no external consumers).

- [ ] **Step 2: Delete `packages/core/`**

```bash
rm -rf packages/core/
```

- [ ] **Step 3: Remove `@pane/core` from `knip.json` workspaces**

In `knip.json`, remove the `"packages/core": {}` line from the `"workspaces"` object.

- [ ] **Step 4: Delete empty `main.js`**

```bash
rm main.js
```

- [ ] **Step 5: Verify all checks pass**

Run:
```bash
bun turbo run typecheck && bun turbo run build && bun knip
```
Expected: typecheck 7/7 (one fewer package), build 4/4, knip clean.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
chore: delete unused @pane/core package and empty main.js

@pane/core had drifted from actual runtime types and had zero consumers.
EOF
)"
```

---

## Task 3: Add `activeProfileId` to `tabStore`

**Files:**
- Modify: `apps/desktop/src/stores/tab-store.ts`
- Modify: `apps/desktop/src/main/profile-tabs.ts`
- Modify: `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`

- [ ] **Step 1: Update `tabStore` with `activeProfileId`**

Replace the entire contents of `apps/desktop/src/stores/tab-store.ts` with:

```ts
import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

interface TabState {
	activeTabId: string | null;
	activeProfileId: string | null;

	setActiveTab: (tabId: string | null, profileId: string | null) => void;
}

export const tabStore = createStore<TabState>()(
	sync(
		(set) => ({
			activeTabId: null,
			activeProfileId: null,

			setActiveTab: (tabId, profileId) =>
				set({ activeTabId: tabId, activeProfileId: profileId }),
		}),
		{ name: "tab-store" },
	),
);
```

- [ ] **Step 2: Update `ProfileTabs` call sites**

In `apps/desktop/src/main/profile-tabs.ts`, make three changes:

**2a.** In `activate()` (around line 94), change:
```ts
tabStore.getState().setActiveTab(tabId);
```
to:
```ts
tabStore.getState().setActiveTab(tabId, this.profile.id);
```

**2b.** In `close()` (around lines 63-67), change:
```ts
if (tabStore.getState().activeTabId === tabId) {
	const allTabs = profileStore.getState().profiles.flatMap((p) => p.tabs);
	const nextId = allTabs[allTabs.length - 1]?.id ?? null;
	tabStore.getState().setActiveTab(nextId);
}
```
to:
```ts
if (tabStore.getState().activeTabId === tabId) {
	const profiles = profileStore.getState().profiles;
	const allTabs = profiles.flatMap((p) => p.tabs);
	const nextTab = allTabs[allTabs.length - 1];
	const nextProfileId = nextTab
		? profiles.find((p) => p.tabs.some((t) => t.id === nextTab.id))?.id ?? null
		: null;
	tabStore.getState().setActiveTab(nextTab?.id ?? null, nextProfileId);
}
```

- [ ] **Step 3: Simplify `address-bar-connected.tsx`**

Replace the entire contents of `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx` with:

```tsx
import { useState } from "react";
import { useStore } from "zustand/react";
import { extensionStore } from "../../../stores/extension-store";
import {
	ProfileColor,
	type ProfileColor as ProfileColorType,
	profileStore,
} from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import {
	AddressBar,
	AddressBarExtensions,
	AddressBarInput,
	AddressBarNav,
	AddressBarProfileBadge,
} from "./address-bar";
import { BrowserActionList } from "./browser-action-list";

export function BrowserAddressBar() {
	const activeTabId = useStore(tabStore, (s) => s.activeTabId);
	const activeProfileId = useStore(tabStore, (s) => s.activeProfileId);
	const profiles = useStore(profileStore, (s) => s.profiles);
	const extensions = useStore(extensionStore, (s) => s.extensions);

	const activeProfile = profiles.find((p) => p.id === activeProfileId);
	const activeTab = activeProfile?.tabs.find((t) => t.id === activeTabId);

	const activeUrl = activeTab?.url ?? "";
	const profileName = activeProfile?.name ?? "";
	const profileColor: ProfileColorType = activeProfile?.color ?? ProfileColor.BLUE;

	const profileExtensions = activeProfileId
		? extensions[activeProfileId]
		: undefined;

	const [inputValue, setInputValue] = useState("");
	const [isFocused, setIsFocused] = useState(false);

	const displayUrl = isFocused ? inputValue : activeUrl;

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();

		if (inputValue.trim()) {
			window.pane.tabs.navigate(inputValue.trim());
			(document.activeElement as HTMLElement)?.blur();
		}
	};

	if (!activeTabId) {
		return null;
	}

	return (
		<AddressBar>
			<AddressBarNav
				onBack={() => window.pane.tabs.goBack()}
				onForward={() => window.pane.tabs.goForward()}
				onReload={() => window.pane.tabs.reload()}
			/>

			<form onSubmit={handleSubmit} className="flex flex-1">
				<AddressBarInput
					value={displayUrl}
					onChange={(e) => setInputValue(e.target.value)}
					onFocus={() => {
						setInputValue(activeUrl);
						setIsFocused(true);
					}}
					onBlur={() => setIsFocused(false)}
					placeholder="Search or enter URL"
				/>
			</form>

			<AddressBarExtensions>
				{profileExtensions && profileExtensions.length > 0 && (
					<BrowserActionList partition={`persist:profile-${activeProfileId}`} />
				)}
			</AddressBarExtensions>

			{profileName ? (
				<AddressBarProfileBadge color={profileColor}>
					{profileName}
				</AddressBarProfileBadge>
			) : null}
		</AddressBar>
	);
}
```

- [ ] **Step 4: Verify typecheck and build**

Run:
```bash
bun turbo run typecheck && bun turbo run build
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/stores/tab-store.ts apps/desktop/src/main/profile-tabs.ts apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx
git commit -m "$(cat <<'EOF'
feat: add activeProfileId to tabStore

Eliminates O(profiles*tabs) linear scans when resolving the active
profile. The renderer reads activeProfileId directly instead of
iterating all profiles to find which one owns the active tab.
EOF
)"
```

---

## Task 4: Deduplicate `serializeState`

**Files:**
- Create: `apps/desktop/src/stores/serialize.ts`
- Modify: `apps/desktop/src/main/store-sync.ts`
- Modify: `apps/desktop/src/stores/middlewares/sync.ts`

- [ ] **Step 1: Create shared `serialize.ts`**

Create `apps/desktop/src/stores/serialize.ts` with:

```ts
export function serializeState(state: unknown): string {
	return JSON.stringify(state, (_key, value) =>
		typeof value === "function" ? undefined : value,
	);
}
```

- [ ] **Step 2: Update `store-sync.ts` to use shared utility**

In `apps/desktop/src/main/store-sync.ts`:

Remove the local `serialize` function (lines 6-9):
```ts
function serialize(state: unknown): string {
	return JSON.stringify(state, (_key, value) =>
		typeof value === "function" ? undefined : value,
	);
}
```

Add the import at the top (after the electron import):
```ts
import { serializeState } from "../stores/serialize";
```

Replace all three usages of `serialize(` with `serializeState(`:
- Line 38: `return store ? serializeState(store.getState()) : null;`
- Line 42: `store.subscribe(() => this.broadcast(name, serializeState(store.getState())));`

- [ ] **Step 3: Update `middlewares/sync.ts` to use shared utility**

In `apps/desktop/src/stores/middlewares/sync.ts`:

Remove the local `serializeState` function (lines 7-10):
```ts
function serializeState<T>(state: T): string {
	return JSON.stringify(state, (_key, value) =>
		typeof value === "function" ? undefined : value,
	);
}
```

Add the import at the top (after the zustand import):
```ts
import { serializeState } from "../serialize";
```

The single usage on line 38 (`serializeState(get())`) already matches the new function name, so no further changes.

- [ ] **Step 4: Verify typecheck and build**

Run:
```bash
bun turbo run typecheck && bun turbo run build
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/stores/serialize.ts apps/desktop/src/main/store-sync.ts apps/desktop/src/stores/middlewares/sync.ts
git commit -m "$(cat <<'EOF'
refactor: deduplicate serializeState into shared utility

Both store-sync.ts and middlewares/sync.ts had identical
JSON-stringify-stripping-functions implementations.
EOF
)"
```

---

## Task 5: Extract `ProfileColor` to `profile-colors.ts`

**Files:**
- Create: `apps/desktop/src/stores/profile-colors.ts`
- Modify: `apps/desktop/src/stores/profile-store.ts`
- Modify: `apps/desktop/src/renderer/components/address-bar/address-bar.tsx`
- Modify: `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`
- Modify: `apps/desktop/src/renderer/components/color-picker.tsx`
- Modify: `apps/desktop/src/renderer/components/create-profile-sheet.tsx`
- Modify: `apps/desktop/src/renderer/components/sidebar/profile-item.tsx`

- [ ] **Step 1: Create `profile-colors.ts`**

Create `apps/desktop/src/stores/profile-colors.ts` with:

```ts
export enum ProfileColor {
	BLUE = "BLUE",
	ROSE = "ROSE",
	EMERALD = "EMERALD",
	AMBER = "AMBER",
	VIOLET = "VIOLET",
	ORANGE = "ORANGE",
	TEAL = "TEAL",
	FUCHSIA = "FUCHSIA",
	ZINC = "ZINC",
}

export const PROFILE_COLOR_HEX: Record<ProfileColor, string> = {
	[ProfileColor.BLUE]: "#60a5fa",
	[ProfileColor.ROSE]: "#fb7185",
	[ProfileColor.EMERALD]: "#34d399",
	[ProfileColor.AMBER]: "#fbbf24",
	[ProfileColor.VIOLET]: "#a78bfa",
	[ProfileColor.ORANGE]: "#f97316",
	[ProfileColor.TEAL]: "#2dd4bf",
	[ProfileColor.FUCHSIA]: "#e879f9",
	[ProfileColor.ZINC]: "#a1a1aa",
};
```

- [ ] **Step 2: Update `profile-store.ts`**

In `apps/desktop/src/stores/profile-store.ts`:

Remove the `ProfileColor` enum (lines 35-45) and `PROFILE_COLOR_HEX` map (lines 47-57).

Add an import at the top (after the middleware imports):
```ts
import { ProfileColor } from "./profile-colors";
```

Add a re-export so existing consumers that import from `profile-store` keep working during the transition:
```ts
export { ProfileColor, PROFILE_COLOR_HEX } from "./profile-colors";
```

Place this re-export right after the other imports, before the `Fingerprint` interface.

- [ ] **Step 3: Update renderer component imports**

For each of these 5 files, update the import to source `ProfileColor` and/or `PROFILE_COLOR_HEX` from `profile-colors` instead of `profile-store`:

**`address-bar.tsx`:** Change import from `"../../../stores/profile-store"` to `"../../../stores/profile-colors"` for `PROFILE_COLOR_HEX` and `ProfileColor`.

**`address-bar-connected.tsx`:** Change import so `ProfileColor` and `type ProfileColor as ProfileColorType` come from `"../../../stores/profile-colors"`, and only `profileStore` comes from `"../../../stores/profile-store"`.

**`color-picker.tsx`:** Change import from `"../../stores/profile-store"` to `"../../stores/profile-colors"` for `PROFILE_COLOR_HEX` and `ProfileColor`.

**`create-profile-sheet.tsx`:** Change import so `ProfileColor` comes from `"../../stores/profile-colors"`, and only `type Fingerprint` and `profileStore` come from `"../../stores/profile-store"`.

**`profile-item.tsx`:** Change import from `"../../../stores/profile-store"` to `"../../../stores/profile-colors"` for `PROFILE_COLOR_HEX` and `type ProfileColor`.

- [ ] **Step 4: Verify typecheck and build**

Run:
```bash
bun turbo run typecheck && bun turbo run build
```
Expected: all pass.

- [ ] **Step 5: Remove re-exports from `profile-store.ts`**

Now that all consumers import directly from `profile-colors.ts`, remove the re-export line from `profile-store.ts`:
```ts
export { ProfileColor, PROFILE_COLOR_HEX } from "./profile-colors";
```

Run knip to confirm nothing still depends on the re-export:
```bash
bun knip
```
Expected: clean. If knip reports unused exports, some consumer was missed — fix the import and retry.

- [ ] **Step 6: Verify all quality gates**

Run:
```bash
bun turbo run typecheck && bun turbo run build && bun eslint && bun biome check --max-diagnostics 500 && bun knip
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add apps/desktop/src/stores/profile-colors.ts apps/desktop/src/stores/profile-store.ts apps/desktop/src/renderer/
git commit -m "$(cat <<'EOF'
refactor: extract ProfileColor and PROFILE_COLOR_HEX to profile-colors.ts

Separates presentation constants from store logic. Both the store
and renderer components import from the same dedicated file.
EOF
)"
```

---

## Task 6: Extract IPC handlers to `IpcRouter` and clean up Pane API

**Files:**
- Create: `apps/desktop/src/main/ipc.ts`
- Modify: `apps/desktop/src/main/pane.ts`
- Modify: `apps/desktop/src/main/pane-extensions.ts`
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Create `ipc.ts` with the `IpcRouter` class**

Create `apps/desktop/src/main/ipc.ts` with:

```ts
import { ipcMain } from "electron";

import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { detectBrowserPath } from "./browser/detect-browser";
import type { Pane } from "./pane";
import type { Profile } from "./profile";

export class IpcRouter {
	constructor(private readonly pane: Pane) {}

	register(): void {
		ipcMain.handle("profiles:activate", (_e, profileId: string) => {
			this.pane.getOrCreateProfile(profileId).extensions.ensureLoaded();
		});

		ipcMain.handle("tabs:open", (_e, profileId: string, url?: string) => {
			this.pane.hideAllTabs();
			this.pane.getOrCreateProfile(profileId).tabs.open(url);
		});

		ipcMain.handle("tabs:close", (_e, tabId: string) => {
			for (const profile of this.pane.profiles.values()) {
				if (profile.tabs.has(tabId)) {
					profile.tabs.close(tabId);

					return;
				}
			}
		});

		ipcMain.handle("tabs:switch", (_e, tabId: string) => {
			this.pane.hideAllTabs();

			for (const profile of this.pane.profiles.values()) {
				if (profile.tabs.has(tabId)) {
					profile.tabs.activate(tabId);

					return;
				}
			}
		});

		ipcMain.handle("tabs:navigate", (_e, url: string) => {
			this.findProfileForActiveTab()?.tabs.navigate(url);
		});

		ipcMain.handle("tabs:go-back", () => {
			this.findProfileForActiveTab()?.tabs.goBack();
		});

		ipcMain.handle("tabs:go-forward", () => {
			this.findProfileForActiveTab()?.tabs.goForward();
		});

		ipcMain.handle("tabs:reload", () => {
			this.findProfileForActiveTab()?.tabs.reload();
		});

		ipcMain.handle("tabs:hide-all", () => {
			this.pane.hideAllTabs();
		});

		ipcMain.handle("tabs:show-active", () => {
			this.findProfileForActiveTab()?.tabs.showActive();
		});

		ipcMain.handle("cws:install", (_e, extensionId: string) =>
			this.pane.extensions.install(extensionId),
		);

		ipcMain.handle("cws:uninstall", (_e, extensionId: string) =>
			this.pane.extensions.uninstall(extensionId),
		);

		ipcMain.handle("cws:installed", () => this.pane.extensions.getInstalled());

		ipcMain.handle("extensions:list", (_e, profileId: string) => {
			const loaded =
				this.pane.getProfile(profileId)?.extensions.getLoaded() ?? [];

			return loaded.map((ext) => ({
				id: ext.id,
				name: ext.name,
				version: ext.manifest.version,
			}));
		});

		ipcMain.handle("settings:detect-browser", () => {
			const detected = detectBrowserPath();

			if (detected) {
				settingsStore.getState().save({ chromiumPath: detected });
			}

			return detected;
		});
	}

	private findProfileForActiveTab(): Profile | undefined {
		const { activeProfileId } = tabStore.getState();

		return activeProfileId
			? this.pane.getProfile(activeProfileId)
			: undefined;
	}
}
```

- [ ] **Step 2: Remove `registerIpc()` from `Pane`, expose `profiles`, rename methods**

In `apps/desktop/src/main/pane.ts`:

1. Delete the entire `registerIpc()` method (lines 107-253).
2. Remove these imports that are no longer needed in this file:
   - `ipcMain` from `"electron"` (keep `path`, `app`, `type BaseWindow`)
   - `settingsStore` from `"../stores/settings-store"`
   - `tabStore` from `"../stores/tab-store"`
   - `detectBrowserPath` from `"./browser/detect-browser"`
3. Change `private readonly profiles` to `readonly profiles` (make it public).
4. Delete the `allProfiles()` method entirely (line 54-56).
5. Update the internal call in `restore()` (line 80): `this.extensions.installed()` → `this.extensions.getInstalled()`.

In `apps/desktop/src/main/pane-extensions.ts`:

1. Rename `installed()` to `getInstalled()` (the method definition).
2. In the `PaneExtensionsHost` interface, replace `allProfiles(): Profile[]` with `readonly profiles: ReadonlyMap<string, Profile>`.
3. Update all internal calls from `this.host.allProfiles()` to `this.host.profiles.values()` (3 occurrences).

- [ ] **Step 3: Update `index.ts` to use `IpcRouter`**

In `apps/desktop/src/main/index.ts`:

Add the import:
```ts
import { IpcRouter } from "./ipc";
```

Replace line 175:
```ts
pane?.registerIpc();
```
with:
```ts
if (pane) {
	new IpcRouter(pane).register();
}
```

- [ ] **Step 4: Verify typecheck and build**

Run:
```bash
bun turbo run typecheck && bun turbo run build
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/main/ipc.ts apps/desktop/src/main/pane.ts apps/desktop/src/main/pane-extensions.ts apps/desktop/src/main/index.ts
git commit -m "$(cat <<'EOF'
refactor: extract IPC handlers to IpcRouter, clean up Pane API

Moves 150 lines of IPC registration out of Pane into a dedicated
IpcRouter. Uses activeProfileId for O(1) active profile lookup.
Exposes profiles Map directly, renames installed->getInstalled.
EOF
)"
```

---

## Task 7: Extract hardcoded fingerprints

**Files:**
- Create: `apps/desktop/src/stores/default-fingerprints.ts`
- Modify: `apps/desktop/src/renderer/components/create-profile-sheet.tsx`

- [ ] **Step 1: Create `default-fingerprints.ts`**

Create `apps/desktop/src/stores/default-fingerprints.ts` with:

```ts
import type { Fingerprint } from "./profile-store";

export const DEFAULT_FINGERPRINTS: Record<string, Fingerprint> = {
	windows: {
		userAgent:
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
		platform: "windows",
		screen: { width: 1920, height: 1080 },
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: {
			vendor: "Google Inc. (NVIDIA)",
			renderer:
				"ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)",
		},
		hardwareConcurrency: 8,
		deviceMemory: 16,
		maxTouchPoints: 0,
	},
	macos: {
		userAgent:
			"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
		platform: "macos",
		screen: { width: 1440, height: 900 },
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: {
			vendor: "Google Inc. (Apple)",
			renderer: "ANGLE (Apple, Apple M1, OpenGL 4.1)",
		},
		hardwareConcurrency: 8,
		deviceMemory: 8,
		maxTouchPoints: 0,
	},
	linux: {
		userAgent:
			"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
		platform: "linux",
		screen: { width: 1920, height: 1080 },
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: {
			vendor: "Google Inc. (Intel)",
			renderer: "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, OpenGL 4.6)",
		},
		hardwareConcurrency: 4,
		deviceMemory: 8,
		maxTouchPoints: 0,
	},
};
```

- [ ] **Step 2: Update `create-profile-sheet.tsx`**

In `apps/desktop/src/renderer/components/create-profile-sheet.tsx`:

Remove the entire `FINGERPRINTS` constant (the `const FINGERPRINTS: Record<string, Fingerprint> = { ... };` block, approximately lines 41-91).

Add the import:
```ts
import { DEFAULT_FINGERPRINTS } from "../../stores/default-fingerprints";
```

Replace the usage on line ~154 (inside `onSubmit`):
```ts
fingerprint: FINGERPRINTS[data.platform],
```
with:
```ts
fingerprint: DEFAULT_FINGERPRINTS[data.platform],
```

Also remove the `type Fingerprint` import from `"../../stores/profile-store"` since it's no longer needed in this file.

- [ ] **Step 3: Verify typecheck and build**

Run:
```bash
bun turbo run typecheck && bun turbo run build
```
Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/stores/default-fingerprints.ts apps/desktop/src/renderer/components/create-profile-sheet.tsx
git commit -m "$(cat <<'EOF'
refactor: extract default fingerprints from create-profile-sheet

Domain data now lives in stores/default-fingerprints.ts instead of
being embedded in a React form component.
EOF
)"
```

---

## Task 8: Fix `require()` in `fs-storage.ts`

**Files:**
- Modify: `apps/desktop/src/stores/middlewares/fs-storage.ts`

- [ ] **Step 1: Replace `require()` calls with static ESM imports**

Replace the entire contents of `apps/desktop/src/stores/middlewares/fs-storage.ts` with:

```ts
import { app } from "electron";
import fs from "node:fs";
import path from "node:path";
import type { StateStorage } from "zustand/middleware";

let dataDir: string | null = null;
const debounceTimers = new Map<string, ReturnType<typeof setTimeout>>();

const DEBOUNCE_MS = 300;

function resolvePath(name: string): string {
	if (!dataDir) {
		dataDir = app.getPath("userData");
	}

	return path.join(dataDir, `${name}.json`);
}

export const fsStorage: StateStorage = {
	getItem(name: string): string | null {
		if (typeof window !== "undefined") {
			return null;
		}

		try {
			return fs.readFileSync(resolvePath(name), "utf-8");
		} catch {
			return null;
		}
	},

	setItem(name: string, value: string): void {
		if (typeof window !== "undefined") {
			return;
		}

		const existing = debounceTimers.get(name);

		if (existing) {
			clearTimeout(existing);
		}

		debounceTimers.set(
			name,
			setTimeout(() => {
				debounceTimers.delete(name);
				const filePath = resolvePath(name);
				const tmpPath = `${filePath}.tmp`;
				fs.writeFileSync(tmpPath, value, "utf-8");
				fs.renameSync(tmpPath, filePath);
			}, DEBOUNCE_MS),
		);
	},

	removeItem(name: string): void {
		if (typeof window !== "undefined") {
			return;
		}

		try {
			fs.unlinkSync(resolvePath(name));
		} catch {}
	},
};
```

- [ ] **Step 2: Verify build (the critical test for this change)**

Run:
```bash
bun turbo run build
```
Expected: all pass. If the renderer build fails on the `electron` import, fall back: keep `const { app } = require("electron")` inside `resolvePath()` but keep the static `node:fs` and `node:path` imports.

- [ ] **Step 3: Verify full quality gate**

Run:
```bash
bun turbo run typecheck && bun eslint && bun biome check --max-diagnostics 500 && bun knip
```
Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/stores/middlewares/fs-storage.ts
git commit -m "$(cat <<'EOF'
refactor: replace require() with static ESM imports in fs-storage

Uses standard import syntax for electron, node:fs, and node:path
instead of runtime require() calls inside function bodies.
EOF
)"
```

---

## Task 9: Final verification

- [ ] **Step 1: Run the complete quality gate**

```bash
bun turbo run build && bun turbo run typecheck && bun eslint && bun biome check --max-diagnostics 500 && bun knip
```
Expected: build 4/4, typecheck 7/7, eslint clean, biome clean, knip clean.

- [ ] **Step 2: Verify no regressions in linting**

```bash
bun eslint 2>&1 | head -5
bun biome check --max-diagnostics 500 2>&1 | tail -5
```
Expected: no errors, no warnings (outside the electron-chrome-extensions override).
