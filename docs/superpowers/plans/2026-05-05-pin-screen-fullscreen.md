# Full-Screen PIN Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace sheet-based PIN flows with a unified full-screen PIN screen that handles unlock, setup, change, and remove in one component.

**Architecture:** Add `pinScreen` nested object to security store. Expand `lock-screen/page.tsx` into a multi-mode `PinScreen` with a step state machine. Layout gates on both `isLocked` and `pinScreen.mode`. Delete the PIN sheets and simplify settings to use store actions.

**Tech Stack:** Zustand, React, tRPC, Tailwind CSS

**Spec:** `docs/superpowers/specs/2026-05-05-pin-screen-fullscreen-design.md`

---

## File Structure

### Modified Files
- `apps/desktop/src/stores/security-store.ts` — add `pinScreen: { mode, show, dismiss }` nested object
- `apps/desktop/src/renderer/app/lock-screen/page.tsx` — rewrite as multi-mode PinScreen with step state machine
- `apps/desktop/src/renderer/app/layout.tsx` — add `pinScreen.mode` gate alongside `isLocked`
- `apps/desktop/src/renderer/app/settings/page.tsx` — replace surface.open calls with store actions, remove sheet imports

### Deleted Files
- `apps/desktop/src/renderer/sheets/pin-setup.tsx`
- `apps/desktop/src/renderer/sheets/pin-verify.tsx`

---

### Task 1: Add pinScreen to security store

**Files:**
- Modify: `apps/desktop/src/stores/security-store.ts`

- [ ] **Step 1: Update the SecurityState interface and store**

In `apps/desktop/src/stores/security-store.ts`, replace the entire file with:

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

export type PinScreenMode = "SETUP" | "CHANGE" | "REMOVE";

export interface SecurityState {
	pin: Pin | null;
	failedAttempts: number;
	isLocked: boolean;
	pinScreen: {
		mode: PinScreenMode | null;
		show: (mode: PinScreenMode) => void;
		dismiss: () => void;
	};

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
				pinScreen: {
					mode: null,
					show: (mode) =>
						set((s) => ({ pinScreen: { ...s.pinScreen, mode } })),
					dismiss: () =>
						set((s) => ({ pinScreen: { ...s.pinScreen, mode: null } })),
				},

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

Key changes from original:
- Added `PinScreenMode` type export
- Added `pinScreen: { mode, show, dismiss }` nested object to interface and store
- `pinScreen` is NOT persisted (excluded from `partialize`) — it's runtime-only UI state

- [ ] **Step 2: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: errors in `lock-screen/page.tsx` and `settings/page.tsx` (they still reference old types). That's fine — we fix them in later tasks.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/security-store.ts
git commit -m "feat: add pinScreen nested object to security store"
```

---

### Task 2: Rewrite lock-screen page as multi-mode PinScreen

**Files:**
- Modify: `apps/desktop/src/renderer/app/lock-screen/page.tsx`

- [ ] **Step 1: Replace the entire file**

Replace `apps/desktop/src/renderer/app/lock-screen/page.tsx` with:

```tsx
import { cn } from "@pane/ui/cn";
import { ArrowLeft } from "lucide-react";
import { useCallback, useEffect, useState } from "react";
import { useStore } from "zustand/react";

import {
	type PinScreenMode,
	securityStore,
} from "../../../stores/security-store";
import { Numpad, NumpadDots } from "../../components/numpad";
import { trpc } from "../../trpc";

const MAX_ATTEMPTS = 5;

enum Step {
	VERIFY = "VERIFY",
	ENTER = "ENTER",
	CONFIRM = "CONFIRM",
}

function getInitialStep(mode: PinScreenMode | "UNLOCK"): Step {
	switch (mode) {
		case "UNLOCK":
		case "REMOVE":
			return Step.VERIFY;
		case "SETUP":
			return Step.ENTER;
		case "CHANGE":
			return Step.VERIFY;
	}
}

function getTitle(mode: PinScreenMode | "UNLOCK", step: Step): string | null {
	if (mode === "UNLOCK") {
		return null;
	}

	switch (step) {
		case Step.VERIFY:
			return "Enter current PIN";
		case Step.ENTER:
			return "Enter new PIN";
		case Step.CONFIRM:
			return "Confirm PIN";
	}
}

export function PinScreen({ mode }: { mode: PinScreenMode | "UNLOCK" }) {
	const pinLength = useStore(securityStore, (s) => s.pin?.length ?? 0);
	const failedAttempts = useStore(securityStore, (s) => s.failedAttempts);

	const [step, setStep] = useState(() => getInitialStep(mode));
	const [entered, setEntered] = useState("");
	const [firstPin, setFirstPin] = useState("");
	const [shake, setShake] = useState(false);
	const [checking, setChecking] = useState(false);
	const [unlocking, setUnlocking] = useState(false);
	const [error, setError] = useState<string | null>(null);

	const remaining = MAX_ATTEMPTS - failedAttempts;
	const isDismissable = mode !== "UNLOCK";
	const title = getTitle(mode, step);

	const dotLength =
		step === Step.ENTER
			? Math.max(entered.length, 4)
			: step === Step.CONFIRM
				? firstPin.length
				: pinLength;

	const autoSubmitLength =
		step === Step.ENTER ? null : step === Step.CONFIRM ? firstPin.length : pinLength;

	const dismiss = useCallback(() => {
		securityStore.getState().pinScreen.dismiss();
	}, []);

	useEffect(() => {
		if (!isDismissable) {
			return;
		}

		const handle = (e: KeyboardEvent) => {
			if (e.key === "Escape") {
				dismiss();
			}
		};

		window.addEventListener("keydown", handle);

		return () => window.removeEventListener("keydown", handle);
	}, [isDismissable, dismiss]);

	const shakeAndClear = useCallback((errorMsg?: string) => {
		if (errorMsg) {
			setError(errorMsg);
		}

		setShake(true);
		setTimeout(() => {
			setShake(false);
			setEntered("");
			setChecking(false);
		}, 600);
	}, []);

	const handleVerifyResult = useCallback(
		async (pin: string) => {
			setChecking(true);

			const result = await trpc.security.verify.mutate({ pin });

			if (!result.success) {
				if (mode === "UNLOCK" && result.remaining <= 0) {
					await trpc.security.wipe.mutate();

					return;
				}

				shakeAndClear();

				return;
			}

			if (mode === "UNLOCK") {
				setEntered(pin);
				setUnlocking(true);

				return;
			}

			if (mode === "CHANGE") {
				setEntered("");
				setStep(Step.ENTER);
				setChecking(false);
				setError(null);

				return;
			}

			if (mode === "REMOVE") {
				await trpc.security.removePin.mutate({ currentPin: pin });
				dismiss();
			}
		},
		[mode, shakeAndClear, dismiss],
	);

	const handleConfirmResult = useCallback(
		async (pin: string) => {
			if (pin === firstPin) {
				await trpc.security.setPin.mutate({ pin });
				dismiss();
			} else {
				shakeAndClear("PINs don't match");
			}
		},
		[firstPin, shakeAndClear, dismiss],
	);

	const handleContinue = useCallback(() => {
		if (entered.length < 4) {
			return;
		}

		setFirstPin(entered);
		setEntered("");
		setStep(Step.CONFIRM);
		setError(null);
	}, [entered]);

	const handleDigit = useCallback(
		(digit: string) => {
			if (checking) {
				return;
			}

			const next = entered + digit;
			setEntered(next);
			setError(null);

			if (autoSubmitLength !== null && next.length === autoSubmitLength) {
				if (step === Step.VERIFY) {
					handleVerifyResult(next);
				} else if (step === Step.CONFIRM) {
					handleConfirmResult(next);
				}
			}
		},
		[entered, checking, autoSubmitLength, step, handleVerifyResult, handleConfirmResult],
	);

	const handleBackspace = useCallback(() => {
		if (checking) {
			return;
		}

		setEntered((prev) => prev.slice(0, -1));
		setError(null);
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
			{isDismissable && (
				<button
					type="button"
					className="absolute left-6 top-6 text-white/30 transition-colors hover:text-white/60"
					onClick={dismiss}
				>
					<ArrowLeft className="h-5 w-5" />
				</button>
			)}

			{title && (
				<p className="text-[13px] font-light text-white/50">{title}</p>
			)}

			<NumpadDots length={dotLength} filled={entered.length} shake={shake} />

			<Numpad onDigit={handleDigit} onBackspace={handleBackspace} />

			{step === Step.ENTER && entered.length >= 4 && (
				<button
					type="button"
					className="text-[13px] font-light text-white/60 hover:text-white/80"
					onClick={handleContinue}
				>
					Continue with {entered.length}-digit PIN
				</button>
			)}

			{step === Step.ENTER && entered.length < 4 && (
				<p className="text-[13px] font-light text-white/30">
					Enter at least 4 digits
				</p>
			)}

			{error && (
				<p className="text-[13px] font-light text-red-500">{error}</p>
			)}

			{mode === "UNLOCK" && failedAttempts > 0 && (
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

Key changes:
- Renamed from `LockScreenPage` to `PinScreen`
- Accepts `mode` prop: `"UNLOCK" | "SETUP" | "CHANGE" | "REMOVE"`
- Step state machine: `VERIFY → ENTER → CONFIRM`
- Back button (ArrowLeft) and Escape key dismiss for non-UNLOCK modes
- `ENTER` step shows growing dots and "Continue with N-digit PIN" button
- `CONFIRM` step auto-submits and checks match
- `VERIFY` step handles unlock, change-prelude, and remove flows

- [ ] **Step 2: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: errors in `layout.tsx` (references old `LockScreenPage`). Fixed in next task.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/app/lock-screen/page.tsx
git commit -m "feat: rewrite lock screen as multi-mode PinScreen with step state machine"
```

---

### Task 3: Update Layout to gate on pinScreen.mode

**Files:**
- Modify: `apps/desktop/src/renderer/app/layout.tsx`

- [ ] **Step 1: Update the import and add pinScreen.mode gate**

In `apps/desktop/src/renderer/app/layout.tsx`:

Change the import on line 65:

```typescript
// Before:
import { LockScreenPage } from "./lock-screen/page";

// After:
import { PinScreen } from "./lock-screen/page";
```

In the `Layout` component, update the selectors (around line 192):

```typescript
// Before:
const isLocked = useStore(securityStore, (s) => s.isLocked);

// After:
const isLocked = useStore(securityStore, (s) => s.isLocked);
const pinScreenMode = useStore(securityStore, (s) => s.pinScreen.mode);
```

Replace the `isLocked` guard block (lines 198-209):

```typescript
// Before:
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

// After:
	if (isLocked || pinScreenMode !== null) {
		return (
			<div
				className="flex h-screen bg-background text-foreground"
				style={{ WebkitAppRegion: "drag" } as React.CSSProperties}
			>
				<ContentPanel className="m-2 ml-2 flex items-center justify-center">
					<PinScreen mode={isLocked ? "UNLOCK" : pinScreenMode!} />
				</ContentPanel>
			</div>
		);
	}
```

- [ ] **Step 2: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: errors in `settings/page.tsx` (still imports deleted sheets). Fixed in next task.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/app/layout.tsx
git commit -m "feat: Layout gates on pinScreen.mode for full-screen PIN flows"
```

---

### Task 4: Update settings page and delete PIN sheets

**Files:**
- Modify: `apps/desktop/src/renderer/app/settings/page.tsx`
- Delete: `apps/desktop/src/renderer/sheets/pin-setup.tsx`
- Delete: `apps/desktop/src/renderer/sheets/pin-verify.tsx`

- [ ] **Step 1: Update settings page**

In `apps/desktop/src/renderer/app/settings/page.tsx`:

Remove the sheet and surface imports (lines 18-20):

```typescript
// DELETE these lines:
import { PinSetupSheet } from "../../sheets/pin-setup";
import { PinVerifySheet } from "../../sheets/pin-verify";
import { surface } from "../../surface";
```

Replace the entire security section JSX (lines 186-253) with:

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
								onClick={() =>
									securityStore.getState().pinScreen.show("SETUP")
								}
							>
								Set up PIN
							</Button>
						) : (
							<div className="flex gap-2">
								<Button
									variant="outline"
									onClick={() =>
										securityStore.getState().pinScreen.show("CHANGE")
									}
								>
									Change PIN
								</Button>
								<Button
									variant="outline"
									onClick={() =>
										securityStore.getState().pinScreen.show("REMOVE")
									}
								>
									Remove PIN
								</Button>
							</div>
						)}
					</div>
				</div>
```

Also remove `useState` from the imports if it's no longer used elsewhere in the file. Check: `useState` IS still used for `extensions`, `isInstalling`, and `uninstallTarget` — so keep it.

- [ ] **Step 2: Delete the PIN sheet files**

```bash
git rm apps/desktop/src/renderer/sheets/pin-setup.tsx
git rm apps/desktop/src/renderer/sheets/pin-verify.tsx
```

- [ ] **Step 3: Verify typecheck**

```bash
cd apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/renderer/app/settings/page.tsx
git commit -m "feat: settings uses full-screen PIN flows, delete PIN sheets"
```
