# Lock Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in PIN lock screen that gates app access after the splash screen, with anti-forensics wipe on 5 failed attempts.

**Architecture:** New `security-store` (Zustand + fsStorage + sync) holds PIN hash, attempt counter, and lock state. Layout conditionally renders a full-width `LockScreen` page or the normal sidebar + content. PIN verification and anti-forensics wipe run in the main process via tRPC. The numpad component is shared between the lock screen page and settings PIN management sheets.

**Tech Stack:** Zustand, tRPC, bcryptjs, React, Tailwind CSS, crypto.randomBytes

**Spec:** `docs/superpowers/specs/2026-05-05-lock-screen-design.md`

---

## File Structure

### New Files
- `apps/desktop/src/stores/security-store.ts` — Zustand store for PIN hash, failed attempts, lock state
- `apps/desktop/src/main/trpc/routers/security.ts` — tRPC router for verify, setPin, removePin, changePin, wipe
- `apps/desktop/src/main/security/wipe.ts` — Anti-forensics wipe logic (overwrite + delete)
- `apps/desktop/src/renderer/app/lock-screen/page.tsx` — Lock screen page component
- `apps/desktop/src/renderer/components/numpad.tsx` — Shared numpad component (used by lock screen + settings sheets)
- `apps/desktop/src/renderer/sheets/pin-setup.tsx` — Sheet for setting up / changing PIN
- `apps/desktop/src/renderer/sheets/pin-verify.tsx` — Sheet for verifying current PIN before change/remove

### Modified Files
- `apps/desktop/src/stores/middlewares/sync.ts` — Add `"security-store"` to `StoreName` union
- `apps/desktop/src/main/index.ts` — Rehydrate security store on boot, add to context stores
- `apps/desktop/src/main/trpc/router.ts` — Register `securityRouter`
- `apps/desktop/src/main/trpc/trpc.ts` — Add `"security-store"` to `StoreName` in `Context`
- `apps/desktop/src/renderer/app/layout.tsx` — Gate on `isLocked`, render lock screen or normal app
- `apps/desktop/src/renderer/app/settings/page.tsx` — Add security section
- `apps/desktop/package.json` — Add `bcryptjs` dependency

### Prerequisite Refactor (rename `pages/` → `app/`, Next.js convention)
- `apps/desktop/src/renderer/pages/` → `apps/desktop/src/renderer/app/`
- `renderer/app/layout.tsx` — stays as file (not in subfolder)
- `renderer/app/surface.tsx` — stays as file (not in subfolder)
- `renderer/app/browser/index.tsx` → `renderer/app/browser/page.tsx`
- `renderer/app/settings/index.tsx` → `renderer/app/settings/page.tsx`
- `apps/desktop/src/renderer/main.tsx` — Update imports for new paths

---

### Task 1: Prerequisite refactor — rename pages/ to app/ (Next.js convention)

**Files:**
- Rename: `apps/desktop/src/renderer/pages/` → `apps/desktop/src/renderer/app/`
- Rename: `apps/desktop/src/renderer/app/browser/index.tsx` → `apps/desktop/src/renderer/app/browser/page.tsx`
- Rename: `apps/desktop/src/renderer/app/settings/index.tsx` → `apps/desktop/src/renderer/app/settings/page.tsx`
- Modify: `apps/desktop/src/renderer/main.tsx`
- Modify: `apps/desktop/src/renderer/app/layout.tsx` (import paths unchanged — same depth)

- [ ] **Step 1: Rename pages/ directory to app/**

```bash
git mv apps/desktop/src/renderer/pages apps/desktop/src/renderer/app
```

- [ ] **Step 2: Rename browser and settings index.tsx to page.tsx**

```bash
git mv apps/desktop/src/renderer/app/browser/index.tsx apps/desktop/src/renderer/app/browser/page.tsx
git mv apps/desktop/src/renderer/app/settings/index.tsx apps/desktop/src/renderer/app/settings/page.tsx
```

- [ ] **Step 3: Update imports in layout.tsx**

`layout.tsx` stayed at the same depth (`app/layout.tsx` vs `pages/layout.tsx`), so store/component imports stay the same. Only the page imports change:

In `apps/desktop/src/renderer/app/layout.tsx`:

```typescript
// Before:
import { BrowserPage } from "./browser";
import { SettingsPage } from "./settings";

// After:
import { BrowserPage } from "./browser/page";
import { SettingsPage } from "./settings/page";
```

- [ ] **Step 4: Update imports in main.tsx**

In `apps/desktop/src/renderer/main.tsx`:

```typescript
// Before:
import { ErrorBoundary, Layout } from "./pages/layout";
import { SurfaceLayout } from "./pages/surface";

// After:
import { ErrorBoundary, Layout } from "./app/layout";
import { SurfaceLayout } from "./app/surface";
```

- [ ] **Step 5: Verify build**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 6: Commit**

```bash
git add -A apps/desktop/src/renderer
git commit -m "refactor: rename pages/ to app/ with Next.js filesystem convention"
```

---

### Task 2: Install bcryptjs and create security store

**Files:**
- Modify: `apps/desktop/package.json`
- Create: `apps/desktop/src/stores/security-store.ts`
- Modify: `apps/desktop/src/stores/middlewares/sync.ts`
- Modify: `apps/desktop/src/stores/middlewares/fs-storage.ts`

- [ ] **Step 1: Install bcryptjs**

```bash
cd /Users/andrevictor/www/pane && pnpm add bcryptjs -F @pane/desktop && pnpm add -D @types/bcryptjs -F @pane/desktop
```

- [ ] **Step 2: Add `"security-store"` to the sync StoreName union**

In `apps/desktop/src/stores/middlewares/sync.ts`, update the `StoreName` type:

```typescript
// Before (line 5-10):
export type StoreName =
	| "profile-store"
	| "tab-store"
	| "navigation-store"
	| "settings-store"
	| "extension-store";

// After:
export type StoreName =
	| "profile-store"
	| "tab-store"
	| "navigation-store"
	| "settings-store"
	| "extension-store"
	| "security-store";
```

- [ ] **Step 3: Add `writeImmediate` to fsStorage**

The `failedAttempts` counter must bypass the 300ms debounce. Add a `writeImmediate` export to `apps/desktop/src/stores/middlewares/fs-storage.ts`:

```typescript
// Add after the fsStorage export (after line 85):

export function writeImmediate(name: string, value: string): void {
	if (typeof window !== "undefined") {
		return;
	}

	const existing = pendingWrites.get(name);

	if (existing) {
		clearTimeout(existing.timer);
		pendingWrites.delete(name);
	}

	const filePath = resolvePath(name);
	const tmpPath = `${filePath}.tmp`;
	const nodeFs = getFs();
	nodeFs.writeFileSync(tmpPath, value, "utf-8");
	nodeFs.renameSync(tmpPath, filePath);
}
```

- [ ] **Step 4: Create the security store**

Create `apps/desktop/src/stores/security-store.ts`:

```typescript
import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";

import { fsStorage, writeImmediate } from "./middlewares/fs-storage";
import { serializeState } from "./middlewares/serialize";
import { sync } from "./middlewares/sync";

export interface Pin {
	hash: string;
	length: number;
}

export interface SecurityState {
	pin: Pin | null;
	failedAttempts: number;
	isLocked: boolean;

	setPin: (pin: Pin) => void;
	clearPin: () => void;
	lock: () => void;
	unlock: () => void;
	recordFailedAttempt: () => number;
	resetAttempts: () => void;
}

export const securityStore = createStore<SecurityState>()(
	persist(
		sync(
			(set, get) => ({
				pin: null,
				failedAttempts: 0,
				isLocked: false,

				setPin: (pin) => set({ pin }),
				clearPin: () => set({ pin: null, isLocked: false, failedAttempts: 0 }),
				lock: () => set({ isLocked: true }),
				unlock: () => set({ isLocked: false }),

				recordFailedAttempt: () => {
					const next = get().failedAttempts + 1;
					set({ failedAttempts: next });

					const state = get();
					const serialized = serializeState({
						pin: state.pin,
						failedAttempts: state.failedAttempts,
						isLocked: state.isLocked,
					});
					writeImmediate("security", serialized);

					return next;
				},

				resetAttempts: () => set({ failedAttempts: 0 }),
			}),
			{ name: "security-store" },
		),
		{
			name: "security",
			storage: createJSONStorage(() => fsStorage),
			skipHydration: true,
			partialize: (state) => ({
				pin: state.pin,
				failedAttempts: state.failedAttempts,
				isLocked: state.isLocked,
			}),
			merge: (persisted, current) => ({
				...current,
				...(persisted as Partial<SecurityState>),
			}),
		},
	),
);
```

- [ ] **Step 5: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 6: Commit**

```bash
git add apps/desktop/package.json apps/desktop/src/stores/security-store.ts apps/desktop/src/stores/middlewares/sync.ts apps/desktop/src/stores/middlewares/fs-storage.ts pnpm-lock.yaml
git commit -m "feat: add security store with PIN and failed attempt tracking"
```

---

### Task 3: Wire security store into main process boot

**Files:**
- Modify: `apps/desktop/src/main/index.ts`
- Modify: `apps/desktop/src/main/trpc/trpc.ts`

- [ ] **Step 1: Add security store to main index**

In `apps/desktop/src/main/index.ts`, add the import:

```typescript
// After line 25 (import { settingsStore }...):
import { securityStore } from "../stores/security-store";
```

Add rehydration after the existing rehydrations (inside `app.whenReady().then`):

```typescript
// After line 116 (settingsStore.persist.rehydrate()):
securityStore.persist.rehydrate();

if (securityStore.getState().pin !== null) {
	securityStore.getState().lock();
}
```

Add `"security-store"` to the context stores map:

```typescript
// In the createContext function, add to the stores object (after line 58):
"security-store": securityStore,
```

- [ ] **Step 2: Update tRPC Context type**

In `apps/desktop/src/main/trpc/trpc.ts`, the `StoreName` type is already re-exported from `sync.ts`, and the `Context.stores` uses `Record<StoreName, StoreApi<object>>`, so adding the store to the `createContext` map is sufficient. No changes needed here — it picks up the new union member automatically.

- [ ] **Step 3: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/index.ts
git commit -m "feat: rehydrate security store on boot and lock if PIN is set"
```

---

### Task 4: Create security tRPC router

**Files:**
- Create: `apps/desktop/src/main/trpc/routers/security.ts`
- Modify: `apps/desktop/src/main/trpc/router.ts`

- [ ] **Step 1: Create the security router**

Create `apps/desktop/src/main/trpc/routers/security.ts`:

```typescript
import bcrypt from "bcryptjs";
import { z } from "zod/v4";

import type { SecurityState } from "../../../stores/security-store";
import { procedure, router } from "../trpc";

const SALT_ROUNDS = 10;

export const securityRouter = router({
	verify: procedure
		.input(z.object({ pin: z.string() }))
		.mutation(async ({ input, ctx }) => {
			const state = ctx.stores["security-store"].getState() as SecurityState;

			if (!state.pin) {
				return { success: false, remaining: 0 };
			}

			const match = await bcrypt.compare(input.pin, state.pin.hash);

			if (match) {
				state.resetAttempts();

				return { success: true, remaining: 5 };
			}

			const attempts = state.recordFailedAttempt();

			return { success: false, remaining: 5 - attempts };
		}),

	setPin: procedure
		.input(z.object({ pin: z.string().min(1) }))
		.mutation(async ({ input, ctx }) => {
			const hash = await bcrypt.hash(input.pin, SALT_ROUNDS);
			const state = ctx.stores["security-store"].getState() as SecurityState;

			state.setPin({ hash, length: input.pin.length });
		}),

	changePin: procedure
		.input(z.object({ currentPin: z.string(), newPin: z.string().min(1) }))
		.mutation(async ({ input, ctx }) => {
			const state = ctx.stores["security-store"].getState() as SecurityState;

			if (!state.pin) {
				return { success: false };
			}

			const match = await bcrypt.compare(input.currentPin, state.pin.hash);

			if (!match) {
				return { success: false };
			}

			const hash = await bcrypt.hash(input.newPin, SALT_ROUNDS);
			state.setPin({ hash, length: input.newPin.length });

			return { success: true };
		}),

	removePin: procedure
		.input(z.object({ currentPin: z.string() }))
		.mutation(async ({ input, ctx }) => {
			const state = ctx.stores["security-store"].getState() as SecurityState;

			if (!state.pin) {
				return { success: false };
			}

			const match = await bcrypt.compare(input.currentPin, state.pin.hash);

			if (!match) {
				return { success: false };
			}

			state.clearPin();

			return { success: true };
		}),
});
```

- [ ] **Step 2: Register the security router**

In `apps/desktop/src/main/trpc/router.ts`, add:

```typescript
// Add import (after line 3):
import { securityRouter } from "./routers/security";

// Add to the router call (after line 13, the ui: uiRouter line):
security: securityRouter,
```

- [ ] **Step 3: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/trpc/routers/security.ts apps/desktop/src/main/trpc/router.ts
git commit -m "feat: add security tRPC router for PIN verify, set, change, remove"
```

---

### Task 5: Create anti-forensics wipe

**Files:**
- Create: `apps/desktop/src/main/security/wipe.ts`
- Modify: `apps/desktop/src/main/trpc/routers/security.ts`

- [ ] **Step 1: Create the wipe module**

Create `apps/desktop/src/main/security/wipe.ts`:

```typescript
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { app } from "electron";

import { profileStore } from "../../stores/profile-store";

function overwriteAndDelete(filePath: string, passes = 3): void {
	try {
		const stat = fs.statSync(filePath);
		const size = stat.size;

		const fd = fs.openSync(filePath, "w");

		for (let i = 0; i < passes; i++) {
			const randomData = crypto.randomBytes(size);
			fs.writeSync(fd, randomData, 0, size, 0);
			fs.fsyncSync(fd);
		}

		fs.closeSync(fd);
		fs.unlinkSync(filePath);
	} catch {}
}

function overwriteDirectory(dirPath: string, passes = 3): void {
	try {
		const entries = fs.readdirSync(dirPath, { withFileTypes: true });

		for (const entry of entries) {
			const fullPath = path.join(dirPath, entry.name);

			if (entry.isDirectory()) {
				overwriteDirectory(fullPath, passes);
				fs.rmdirSync(fullPath);
			} else {
				overwriteAndDelete(fullPath, passes);
			}
		}
	} catch {}
}

export function executeWipe(): void {
	const userData = app.getPath("userData");
	const tempDir = app.getPath("temp");

	const profiles = profileStore.getState().profiles;

	for (const profile of profiles) {
		const partitionPath = path.join(
			userData,
			"Partitions",
			`persist_profile-${profile.id}`,
		);
		overwriteDirectory(partitionPath);

		try {
			fs.rmdirSync(partitionPath, { recursive: true });
		} catch {}
	}

	overwriteAndDelete(path.join(userData, "profiles.json"));
	overwriteAndDelete(path.join(userData, "security.json"));
	overwriteAndDelete(path.join(userData, "settings.json"));

	const fpDir = path.join(tempDir, "pane-fingerprints");

	try {
		const fpFiles = fs.readdirSync(fpDir);

		for (const file of fpFiles) {
			if (file.startsWith("fp-") && file.endsWith(".js")) {
				overwriteAndDelete(path.join(fpDir, file));
			}
		}
	} catch {}
}
```

- [ ] **Step 2: Add wipe mutation to the security router**

In `apps/desktop/src/main/trpc/routers/security.ts`, add the import and mutation:

```typescript
// Add import at top:
import { app } from "electron";

import { executeWipe } from "../../security/wipe";
```

Add the wipe mutation inside the router (after `removePin`):

```typescript
	wipe: procedure.mutation(({ ctx }) => {
		executeWipe();

		setTimeout(() => {
			app.relaunch();
			app.exit(0);
		}, 100);
	}),
```

- [ ] **Step 3: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/security/wipe.ts apps/desktop/src/main/trpc/routers/security.ts
git commit -m "feat: add anti-forensics wipe with 3-pass random overwrite"
```

---

### Task 6: Create shared numpad component

**Files:**
- Create: `apps/desktop/src/renderer/components/numpad.tsx`

- [ ] **Step 1: Create the numpad component**

Create `apps/desktop/src/renderer/components/numpad.tsx`:

```tsx
import { cn } from "@pane/ui/cn";
import { useCallback, useEffect } from "react";

const DIGITS = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "backspace"] as const;

function BackspaceIcon() {
	return (
		<svg
			width="22"
			height="18"
			viewBox="0 0 24 20"
			fill="none"
			stroke="currentColor"
			strokeWidth={1.2}
			strokeLinecap="round"
			strokeLinejoin="round"
		>
			<path d="M9 2H20a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H9l-7-8 7-8z" />
			<line x1="16" y1="7" x2="12" y2="13" />
			<line x1="12" y1="7" x2="16" y2="13" />
		</svg>
	);
}

export function NumpadDots({
	length,
	filled,
	shake,
}: {
	length: number;
	filled: number;
	shake: boolean;
}) {
	return (
		<div
			className={cn("flex gap-3.5", shake && "animate-shake")}
		>
			{Array.from({ length }, (_, i) => (
				<div
					key={i}
					className={cn(
						"h-[13px] w-[13px] rounded-full transition-all duration-150",
						i < filled
							? "scale-100 bg-white"
							: "scale-100 border-[1.5px] border-white/25",
					)}
				/>
			))}
		</div>
	);
}

export function Numpad({
	onDigit,
	onBackspace,
}: {
	onDigit: (digit: string) => void;
	onBackspace: () => void;
}) {
	const handleKey = useCallback(
		(e: KeyboardEvent) => {
			if (e.key >= "0" && e.key <= "9") {
				onDigit(e.key);
			} else if (e.key === "Backspace") {
				onBackspace();
			}
		},
		[onDigit, onBackspace],
	);

	useEffect(() => {
		window.addEventListener("keydown", handleKey);

		return () => window.removeEventListener("keydown", handleKey);
	}, [handleKey]);

	return (
		<div className="grid grid-cols-3 gap-3.5">
			{DIGITS.map((key, i) => {
				if (key === "") {
					return <div key={i} className="h-16 w-16" />;
				}

				if (key === "backspace") {
					return (
						<button
							key={i}
							type="button"
							className="flex h-16 w-16 items-center justify-center rounded-full text-white/35 transition-colors hover:text-white/60 active:text-white/80"
							onClick={onBackspace}
						>
							<BackspaceIcon />
						</button>
					);
				}

				return (
					<button
						key={i}
						type="button"
						className="flex h-16 w-16 items-center justify-center rounded-full border border-white/12 text-[26px] font-extralight text-white/85 transition-colors hover:border-white/20 hover:text-white active:bg-white/10"
						onClick={() => onDigit(key)}
					>
						{key}
					</button>
				);
			})}
		</div>
	);
}
```

- [ ] **Step 2: Add the shake keyframe to globals.css**

In `apps/desktop/src/renderer/styles/globals.css`, add after the theme block (after line 29):

```css
@keyframes shake {
	0%, 100% { transform: translateX(0); }
	20% { transform: translateX(-8px); }
	40% { transform: translateX(8px); }
	60% { transform: translateX(-6px); }
	80% { transform: translateX(6px); }
}

.animate-shake {
	animation: shake 0.4s ease-in-out;
}
```

- [ ] **Step 3: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/renderer/components/numpad.tsx apps/desktop/src/renderer/styles/globals.css
git commit -m "feat: add shared numpad component with dots and shake animation"
```

---

### Task 7: Create lock screen page

**Files:**
- Create: `apps/desktop/src/renderer/app/lock-screen/page.tsx`
- Modify: `apps/desktop/src/renderer/app/layout.tsx`

- [ ] **Step 1: Create the lock screen page**

Create `apps/desktop/src/renderer/app/lock-screen/page.tsx`:

```tsx
import { cn } from "@pane/ui/cn";
import { useCallback, useState } from "react";
import { useStore } from "zustand/react";

import { securityStore } from "../../../stores/security-store";
import { Numpad, NumpadDots } from "../../components/numpad";
import { trpc } from "../../trpc";

const MAX_ATTEMPTS = 5;

export function LockScreenPage() {
	const pinLength = useStore(securityStore, (s) => s.pin?.length ?? 0);
	const failedAttempts = useStore(securityStore, (s) => s.failedAttempts);

	const [entered, setEntered] = useState("");
	const [shake, setShake] = useState(false);
	const [checking, setChecking] = useState(false);
	const [unlocking, setUnlocking] = useState(false);

	const remaining = MAX_ATTEMPTS - failedAttempts;

	const handleResult = useCallback(
		async (pin: string) => {
			if (checking) {
				return;
			}

			setChecking(true);

			const result = await trpc.security.verify.mutate({ pin });

			if (result.success) {
				setEntered(pin);
				setUnlocking(true);

				return;
			}

			if (result.remaining <= 0) {
				await trpc.security.wipe.mutate();

				return;
			}

			setShake(true);
			setTimeout(() => {
				setShake(false);
				setEntered("");
				setChecking(false);
			}, 600);
		},
		[checking],
	);

	const handleDigit = useCallback(
		(digit: string) => {
			if (checking) {
				return;
			}

			const next = entered + digit;
			setEntered(next);

			if (next.length === pinLength) {
				handleResult(next);
			}
		},
		[entered, pinLength, handleResult, checking],
	);

	const handleBackspace = useCallback(() => {
		if (checking) {
			return;
		}

		setEntered((prev) => prev.slice(0, -1));
	}, [checking]);

	return (
		<div
			className={cn(
				"flex flex-col items-center justify-center gap-7 transition-opacity duration-300",
				unlocking && "opacity-0",
			)}
			onTransitionEnd={() => {
				if (unlocking) {
					securityStore.getState().unlock();
				}
			}}
		>
			<NumpadDots length={pinLength} filled={entered.length} shake={shake} />

			<Numpad onDigit={handleDigit} onBackspace={handleBackspace} />

			{failedAttempts > 0 && (
				<p
					className={
						remaining === 1
							? "text-[13px] font-light text-red-500"
							: "text-[13px] font-light text-white/30"
					}
				>
					{remaining === 1
						? "Last attempt. All data will be erased."
						: `${remaining} attempts remaining`}
				</p>
			)}
		</div>
	);
}
```

- [ ] **Step 2: Modify Layout to gate on isLocked**

In `apps/desktop/src/renderer/app/layout.tsx`, add imports:

```typescript
// Add after other store imports:
import { securityStore } from "../../stores/security-store";

// Add after other page imports:
import { LockScreenPage } from "./lock-screen/page";
```

Replace the `Layout` component's return statement. The current `Layout` function (starting around line 183 after the refactor) needs to conditionally render. Update the component:

```typescript
export function Layout({ onReady }: { onReady?: () => void }) {
	const profileIds = useStore(
		profileStore,
		useShallow((s) => s.profiles.map((p) => p.id)),
	);

	const page = useStore(navigationStore, (s) => s.page);
	const isLocked = useStore(securityStore, (s) => s.isLocked);

	useEffect(() => {
		onReady?.();
	}, [onReady]);

	if (isLocked) {
		return (
			<div
				className="flex h-screen bg-background text-foreground"
				style={{ WebkitAppRegion: "drag" } as React.CSSProperties}
			>
				<ContentPanel className="m-2 ml-2 flex items-center justify-center">
					<LockScreenPage />
				</ContentPanel>
			</div>
		);
	}

	return (
		<div
			className="flex h-screen bg-background text-foreground"
			style={{ WebkitAppRegion: "drag" } as React.CSSProperties}
		>
			<Sidebar>
				<SidebarHeader>
					<SidebarTitle>Pane</SidebarTitle>
				</SidebarHeader>

				<SidebarContent>
					{profileIds.map((id) => (
						<SidebarProfileItem key={id} id={id} />
					))}
				</SidebarContent>

				<SidebarFooter>
					<SidebarNewButton onClick={() => surface.open(CreateProfileSheet)} />
					<SidebarSeparator />
					<SidebarSettingsButton
						active={page === Page.SETTINGS}
						onClick={() => navigationStore.getState().navigate(Page.SETTINGS)}
					>
						<Settings className="h-3.5 w-3.5" />
						Settings
					</SidebarSettingsButton>
				</SidebarFooter>
			</Sidebar>

			<ContentPanel>
				{page === Page.BROWSER ? <BrowserPage /> : null}
				{page === Page.SETTINGS ? <SettingsPage /> : null}
			</ContentPanel>
		</div>
	);
}
```

Note: the `ContentPanel` for the lock screen uses `className="m-2 ml-2"` to override the default `ml-0` so the panel has uniform margin on all sides (no sidebar).

- [ ] **Step 3: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/renderer/app/lock-screen/page.tsx apps/desktop/src/renderer/app/layout.tsx
git commit -m "feat: add lock screen page with Layout gate"
```

---

### Task 8: Create PIN setup sheet

**Files:**
- Create: `apps/desktop/src/renderer/sheets/pin-setup.tsx`

- [ ] **Step 1: Create the PIN setup sheet**

Create `apps/desktop/src/renderer/sheets/pin-setup.tsx`:

```tsx
import {
	Sheet,
	SheetBody,
	SheetContent,
	SheetHeader,
	SheetTitle,
} from "@pane/ui/components/sheet";
import { X } from "lucide-react";
import { useCallback, useState } from "react";

import { Numpad, NumpadDots } from "../components/numpad";
import { trpc } from "../trpc";

enum SetupStep {
	ENTER = "ENTER",
	CONFIRM = "CONFIRM",
}

export function PinSetupSheet({ onClose }: { onClose: () => void }) {
	const [step, setStep] = useState(SetupStep.ENTER);
	const [firstPin, setFirstPin] = useState("");
	const [entered, setEntered] = useState("");
	const [shake, setShake] = useState(false);
	const [error, setError] = useState<string | null>(null);

	const pinLength = step === SetupStep.ENTER ? entered.length : firstPin.length;
	const displayLength = step === SetupStep.ENTER ? Math.max(entered.length, 4) : firstPin.length;

	const handleDigit = useCallback(
		(digit: string) => {
			if (step === SetupStep.ENTER) {
				setEntered((prev) => prev + digit);

				return;
			}

			const next = entered + digit;
			setEntered(next);

			if (next.length === firstPin.length) {
				if (next === firstPin) {
					trpc.security.setPin.mutate({ pin: next }).then(onClose);
				} else {
					setError("PINs don't match");
					setShake(true);
					setTimeout(() => {
						setShake(false);
						setEntered("");
					}, 600);
				}
			}
		},
		[step, entered, firstPin, onClose],
	);

	const handleBackspace = useCallback(() => {
		setEntered((prev) => prev.slice(0, -1));
		setError(null);
	}, []);

	const handleConfirmStep = useCallback(() => {
		if (entered.length < 4) {
			return;
		}

		setFirstPin(entered);
		setEntered("");
		setStep(SetupStep.CONFIRM);
		setError(null);
	}, [entered]);

	return (
		<Sheet open onOpenChange={(open) => !open && onClose()}>
			<SheetContent>
				<SheetHeader>
					<SheetTitle>
						{step === SetupStep.ENTER ? "Enter new PIN" : "Confirm PIN"}
					</SheetTitle>
					<button
						type="button"
						onClick={onClose}
						className="text-muted-foreground hover:text-foreground"
					>
						<X className="h-4 w-4" />
					</button>
				</SheetHeader>

				<SheetBody className="flex flex-col items-center gap-7 py-10">
					<NumpadDots
						length={displayLength}
						filled={entered.length}
						shake={shake}
					/>

					<Numpad onDigit={handleDigit} onBackspace={handleBackspace} />

					{step === SetupStep.ENTER && entered.length >= 4 && (
						<button
							type="button"
							className="text-[13px] font-light text-white/60 hover:text-white/80"
							onClick={handleConfirmStep}
						>
							Continue with {entered.length}-digit PIN
						</button>
					)}

					{error && (
						<p className="text-[13px] font-light text-red-500">{error}</p>
					)}

					{step === SetupStep.ENTER && (
						<p className="text-[13px] font-light text-white/30">
							Enter at least 4 digits
						</p>
					)}
				</SheetBody>
			</SheetContent>
		</Sheet>
	);
}
```

- [ ] **Step 2: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/sheets/pin-setup.tsx
git commit -m "feat: add PIN setup sheet with enter and confirm steps"
```

---

### Task 9: Create PIN verify sheet

**Files:**
- Create: `apps/desktop/src/renderer/sheets/pin-verify.tsx`

- [ ] **Step 1: Create the PIN verify sheet**

This sheet verifies the current PIN before allowing change or removal. It accepts `onVerified` callback.

Create `apps/desktop/src/renderer/sheets/pin-verify.tsx`:

```tsx
import {
	Sheet,
	SheetBody,
	SheetContent,
	SheetHeader,
	SheetTitle,
} from "@pane/ui/components/sheet";
import { X } from "lucide-react";
import { useCallback, useState } from "react";
import { useStore } from "zustand/react";

import { securityStore } from "../../stores/security-store";
import { Numpad, NumpadDots } from "../components/numpad";
import { trpc } from "../trpc";

export function PinVerifySheet({
	title,
	onClose,
	onVerified,
}: {
	title: string;
	onClose: () => void;
	onVerified: (currentPin: string) => void;
}) {
	const pinLength = useStore(securityStore, (s) => s.pin?.length ?? 0);

	const [entered, setEntered] = useState("");
	const [shake, setShake] = useState(false);
	const [error, setError] = useState<string | null>(null);
	const [checking, setChecking] = useState(false);

	const handleResult = useCallback(
		async (pin: string) => {
			if (checking) {
				return;
			}

			setChecking(true);

			const result = await trpc.security.verify.mutate({ pin });

			if (result.success) {
				onVerified(pin);

				return;
			}

			setError("Wrong PIN");
			setShake(true);
			setTimeout(() => {
				setShake(false);
				setEntered("");
				setChecking(false);
			}, 600);
		},
		[checking, onVerified],
	);

	const handleDigit = useCallback(
		(digit: string) => {
			if (checking) {
				return;
			}

			const next = entered + digit;
			setEntered(next);

			if (next.length === pinLength) {
				handleResult(next);
			}
		},
		[entered, pinLength, handleResult, checking],
	);

	const handleBackspace = useCallback(() => {
		if (checking) {
			return;
		}

		setEntered((prev) => prev.slice(0, -1));
		setError(null);
	}, [checking]);

	return (
		<Sheet open onOpenChange={(open) => !open && onClose()}>
			<SheetContent>
				<SheetHeader>
					<SheetTitle>{title}</SheetTitle>
					<button
						type="button"
						onClick={onClose}
						className="text-muted-foreground hover:text-foreground"
					>
						<X className="h-4 w-4" />
					</button>
				</SheetHeader>

				<SheetBody className="flex flex-col items-center gap-7 py-10">
					<NumpadDots
						length={pinLength}
						filled={entered.length}
						shake={shake}
					/>

					<Numpad onDigit={handleDigit} onBackspace={handleBackspace} />

					{error && (
						<p className="text-[13px] font-light text-red-500">{error}</p>
					)}
				</SheetBody>
			</SheetContent>
		</Sheet>
	);
}
```

- [ ] **Step 2: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/sheets/pin-verify.tsx
git commit -m "feat: add PIN verify sheet for authenticated PIN changes"
```

---

### Task 10: Add security section to settings page

**Files:**
- Modify: `apps/desktop/src/renderer/app/settings/page.tsx` (renamed in Task 1)

- [ ] **Step 1: Add security section**

In `apps/desktop/src/renderer/app/settings/page.tsx`, add imports:

```typescript
// Add to existing imports:
import { securityStore } from "../../../stores/security-store";
import { surface } from "../../surface";
import { PinSetupSheet } from "../../sheets/pin-setup";
import { PinVerifySheet } from "../../sheets/pin-verify";
```

Inside the `SettingsPage` component, add a selector for pin state:

```typescript
const pin = useStore(securityStore, (s) => s.pin);
```

Add the security section JSX after the extensions section's closing `</div>` and before the `<UninstallDialog>` (around line 179 of the original file):

```tsx
				<div className="h-px bg-[rgba(255,255,255,0.05)]" />

				<div>
					<span className="font-medium text-[10px] text-muted-foreground uppercase tracking-wide">
						Security
					</span>

					<div className="mt-3 space-y-3">
						{pin === null ? (
							<Button
								variant="outline"
								onClick={() => surface.open(PinSetupSheet)}
							>
								Set up PIN
							</Button>
						) : (
							<div className="flex gap-2">
								<Button
									variant="outline"
									onClick={() =>
										surface.open(
											function ChangePinFlow({ onClose }: { onClose: () => void }) {
												const [verified, setVerified] = useState(false);

												if (!verified) {
													return (
														<PinVerifySheet
															title="Enter current PIN"
															onClose={onClose}
															onVerified={() => setVerified(true)}
														/>
													);
												}

												return <PinSetupSheet onClose={onClose} />;
											},
										)
									}
								>
									Change PIN
								</Button>
								<Button
									variant="outline"
									onClick={() =>
										surface.open(
											function RemovePinFlow({ onClose }: { onClose: () => void }) {
												return (
													<PinVerifySheet
														title="Enter current PIN to remove"
														onClose={onClose}
														onVerified={async (currentPin) => {
															await trpc.security.removePin.mutate({
																currentPin,
															});
															onClose();
														}}
													/>
												);
											},
										)
									}
								>
									Remove PIN
								</Button>
							</div>
						)}
					</div>
				</div>
```

Also add the `useState` import if not already present (it is — line 12 of the original).

- [ ] **Step 2: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/app/settings/page.tsx
git commit -m "feat: add security section to settings with PIN setup, change, remove"
```

---

### Task 11: Manual smoke test

- [ ] **Step 1: Start the dev server**

```bash
cd /Users/andrevictor/www/pane && turbo run dev --filter=@pane/desktop
```

- [ ] **Step 2: Test no-PIN boot**

Launch the app. Verify:
- Splash screen fades out into the normal sidebar + content layout
- No lock screen appears
- Settings page shows "Security" section with "Set up PIN" button

- [ ] **Step 3: Test PIN setup**

In Settings → Security:
- Click "Set up PIN"
- Enter a PIN (e.g., 5 digits: 12345)
- Click "Continue with 5-digit PIN"
- Confirm by entering 12345 again
- Verify the sheet closes
- Settings now shows "Change PIN" and "Remove PIN" buttons

- [ ] **Step 4: Test lock screen on restart**

Restart the app (Cmd+Q, relaunch). Verify:
- Splash fades out into the lock screen
- Full-width content panel, no sidebar
- 5 empty PIN dots
- Numpad with minimal outline circles
- Enter correct PIN → app unlocks to normal layout

- [ ] **Step 5: Test wrong PIN feedback**

Restart the app. Enter wrong PIN:
- Dots shake and clear
- "4 attempts remaining" appears
- Enter wrong again → "3 attempts remaining"
- Enter correct PIN → unlocks, counter resets

- [ ] **Step 6: Test keyboard input**

On lock screen, type digits on keyboard instead of clicking numpad. Verify:
- Dots fill as expected
- Backspace removes last digit
- Auto-submits at correct length

- [ ] **Step 7: Test change PIN**

In Settings → Security:
- Click "Change PIN"
- Enter current PIN → moves to setup sheet
- Enter new PIN + confirm
- Restart app → lock screen uses new PIN

- [ ] **Step 8: Test remove PIN**

In Settings → Security:
- Click "Remove PIN"
- Enter current PIN
- Verify sheet closes, button changes back to "Set up PIN"
- Restart app → no lock screen

- [ ] **Step 9: Test anti-forensics wipe**

Set a PIN, restart. Enter wrong PIN 5 times:
- After 5th failure, app should wipe and restart
- On restart: no lock screen, no profiles, clean state
- Verify `userData/profiles.json`, `security.json`, `settings.json` are gone

- [ ] **Step 10: Commit any fixes**

If any issues found during testing, fix and commit individually.
