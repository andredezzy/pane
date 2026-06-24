# Tab Switcher Profile Grouping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the Ctrl+Tab MRU switcher so its tabs are grouped under their profile, with each group showing the profile name.

**Architecture:** Extract the pure grouping/selection logic into a new side-effect-free module (`tab-switcher-grouping.ts`) so it can be unit-tested in the node test env without loading the React/tRPC/store graph — the same seam approach as `sync.test.ts`. The `TabSwitcher` component consumes that module, renders a header per profile group with indented tab rows beneath, and runs keyboard selection over the flattened tab order.

**Tech Stack:** React 19, TypeScript, Zustand (vanilla stores), tRPC-over-IPC, Tailwind, Vitest 4 (node environment), Biome/ESLint.

## Global Constraints

- Switcher stays a lightweight quick-switcher: only the most-recently-used tabs, capped at `MAX_VISIBLE_TABS = 8` (counting **tabs**, not headers).
- Groups appear in **first-seen (recency) order**; tabs within a group keep `mruHistory` order.
- Selection runs over the **flattened** tab order (`groups.flatMap((group) => group.tabs)`); Ctrl+Tab / Ctrl+Shift+Tab advance and wrap.
- Quick-flick preserved: initial selection = flattened position of `mruHistory[1]` (previous tab); fallback `Math.min(1, flattened.length - 1)`.
- Profile name shown in **natural case** (no forced uppercase — matches the repo's sentence-case label convention).
- Per-row color dot **removed**; the group header carries the profile color + name.
- No store, IPC, or data-model changes.
- Commit style: plain imperative subject lines, no `type(scope):` prefix (matches repo history).
- Quality gates: zero TypeScript errors (`bun run typecheck`), zero lint errors (`bun run lint`), zero knip issues (`bun run knip`).

---

## File Structure

- **Create** `apps/veil/src/renderer/components/tab-switcher-grouping.ts` — pure logic + types: `SwitcherTab`, `GroupingProfile`, `ProfileGroup`, `groupMruTabs()`, `initialSelectedIndex()`. Imports only `ProfileColor` (a type). No React, no stores, no tRPC.
- **Create** `apps/veil/src/renderer/components/tab-switcher-grouping.test.ts` — unit tests for the two functions.
- **Modify** `apps/veil/src/renderer/components/tab-switcher.tsx` — consume the grouping module; render grouped headers + indented rows; flatten for selection; seed initial selection from the previous tab.

---

### Task 1: Pure grouping & selection logic

**Files:**
- Create: `apps/veil/src/renderer/components/tab-switcher-grouping.ts`
- Test: `apps/veil/src/renderer/components/tab-switcher-grouping.test.ts`

**Interfaces:**
- Consumes: `ProfileColor` from `../../constants/profile-colors`.
- Produces:
  - `interface SwitcherTab { id: string; title: string; favicon: string }`
  - `interface GroupingProfile { id: string; name: string; color: ProfileColor; tabs: SwitcherTab[] }`
  - `interface ProfileGroup { id: string; name: string; color: ProfileColor; tabs: SwitcherTab[] }`
  - `groupMruTabs(mruHistory: string[], profiles: GroupingProfile[], maxTabs: number): ProfileGroup[]`
  - `initialSelectedIndex(flattened: SwitcherTab[], previousTabId: string | undefined): number`

- [ ] **Step 1: Write the failing tests**

Create `apps/veil/src/renderer/components/tab-switcher-grouping.test.ts`:

```typescript
import { describe, expect, it } from "vitest";

import { ProfileColor } from "../../constants/profile-colors";
import {
	type GroupingProfile,
	groupMruTabs,
	initialSelectedIndex,
} from "./tab-switcher-grouping";

function makeTab(id: string, title = id, favicon = "") {
	return { id, title, favicon };
}

const profiles: GroupingProfile[] = [
	{
		id: "work",
		name: "Work",
		color: ProfileColor.BLUE,
		tabs: [makeTab("w1", "Gmail"), makeTab("w2", "Caixa")],
	},
	{
		id: "personal",
		name: "Personal",
		color: ProfileColor.ROSE,
		tabs: [makeTab("p1", "Proton"), makeTab("p2", "Inbox")],
	},
];

describe("groupMruTabs", () => {
	it("groups tabs by profile in first-seen (recency) order", () => {
		const groups = groupMruTabs(["p1", "w1", "p2"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["personal", "work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["p1", "p2"]);
		expect(groups[1].tabs.map((tab) => tab.id)).toEqual(["w1"]);
	});

	it("caps the total number of tabs across groups", () => {
		const groups = groupMruTabs(["w1", "p1", "w2", "p2"], profiles, 2);

		const total = groups.reduce((sum, group) => sum + group.tabs.length, 0);

		expect(total).toBe(2);
		expect(groups.map((group) => group.id)).toEqual(["work", "personal"]);
	});

	it("skips tab ids that no profile owns", () => {
		const groups = groupMruTabs(["ghost", "w1"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["w1"]);
	});

	it("falls back to a loading title and empty favicon", () => {
		const sparse: GroupingProfile[] = [
			{
				id: "x",
				name: "X",
				color: ProfileColor.TEAL,
				tabs: [{ id: "x1", title: "", favicon: "" }],
			},
		];

		const groups = groupMruTabs(["x1"], sparse, 8);

		expect(groups[0].tabs[0].title).toBe("Loading...");
		expect(groups[0].tabs[0].favicon).toBe("");
	});
});

describe("initialSelectedIndex", () => {
	const flattened = [makeTab("a"), makeTab("b"), makeTab("c")];

	it("selects the previous tab's flattened position", () => {
		expect(initialSelectedIndex(flattened, "c")).toBe(2);
	});

	it("falls back to the second item when the previous tab is gone", () => {
		expect(initialSelectedIndex(flattened, "missing")).toBe(1);
	});

	it("falls back to 0 when only one tab is present", () => {
		expect(initialSelectedIndex([makeTab("only")], undefined)).toBe(0);
	});
});
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd apps/veil && bunx vitest run src/renderer/components/tab-switcher-grouping.test.ts`
Expected: FAIL — cannot resolve `./tab-switcher-grouping` (module not created yet).

- [ ] **Step 3: Write the implementation**

Create `apps/veil/src/renderer/components/tab-switcher-grouping.ts`:

```typescript
import type { ProfileColor } from "../../constants/profile-colors";

export interface SwitcherTab {
	id: string;
	title: string;
	favicon: string;
}

export interface GroupingProfile {
	id: string;
	name: string;
	color: ProfileColor;
	tabs: SwitcherTab[];
}

export interface ProfileGroup {
	id: string;
	name: string;
	color: ProfileColor;
	tabs: SwitcherTab[];
}

export function groupMruTabs(
	mruHistory: string[],
	profiles: GroupingProfile[],
	maxTabs: number,
): ProfileGroup[] {
	const groups: ProfileGroup[] = [];
	const groupsByProfileId = new Map<string, ProfileGroup>();

	let count = 0;

	for (const tabId of mruHistory) {
		if (count >= maxTabs) {
			break;
		}

		for (const profile of profiles) {
			const tab = profile.tabs.find((tab) => tab.id === tabId);

			if (!tab) {
				continue;
			}

			let group = groupsByProfileId.get(profile.id);

			if (!group) {
				group = {
					id: profile.id,
					name: profile.name,
					color: profile.color,
					tabs: [],
				};

				groupsByProfileId.set(profile.id, group);
				groups.push(group);
			}

			group.tabs.push({
				id: tab.id,
				title: tab.title || "Loading...",
				favicon: tab.favicon || "",
			});

			count++;

			break;
		}
	}

	return groups;
}

export function initialSelectedIndex(
	flattened: SwitcherTab[],
	previousTabId: string | undefined,
): number {
	const index = flattened.findIndex((tab) => tab.id === previousTabId);

	return index >= 0 ? index : Math.min(1, flattened.length - 1);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd apps/veil && bunx vitest run src/renderer/components/tab-switcher-grouping.test.ts`
Expected: PASS — 7 tests passing.

- [ ] **Step 5: Commit**

```bash
git add apps/veil/src/renderer/components/tab-switcher-grouping.ts apps/veil/src/renderer/components/tab-switcher-grouping.test.ts
git commit -m "Add profile-grouping logic for the tab switcher"
```

---

### Task 2: Render the switcher grouped by profile

**Files:**
- Modify: `apps/veil/src/renderer/components/tab-switcher.tsx`

**Interfaces:**
- Consumes: `groupMruTabs`, `initialSelectedIndex` from `./tab-switcher-grouping` (Task 1).
- Produces: the updated `TabSwitcher` component (same `{ onClose }` prop, unchanged export name).

This task has no DOM unit test (the test env is `node`; there is no component-rendering setup). It is verified by typecheck, lint, knip, and a manual run.

- [ ] **Step 1: Replace the component file contents**

Overwrite `apps/veil/src/renderer/components/tab-switcher.tsx` with:

```tsx
import { cn } from "@pane/ui/cn";
import { useCallback, useEffect, useRef, useState } from "react";

import { HotkeyEvent } from "../../constants/hotkey-event";
import { PROFILE_COLOR_HEX } from "../../constants/profile-colors";
import { navigationStore, Page } from "../../stores/navigation-store";
import { profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import { trpc } from "../trpc";
import { groupMruTabs, initialSelectedIndex } from "./tab-switcher-grouping";

const MAX_VISIBLE_TABS = 8;

export function TabSwitcher({ onClose }: { onClose: () => void }) {
	const [groups] = useState(() =>
		groupMruTabs(
			tabStore.getState().mruHistory,
			profileStore.getState().profiles,
			MAX_VISIBLE_TABS,
		),
	);

	const flattened = groups.flatMap((group) => group.tabs);

	const [selectedIndex, setSelectedIndex] = useState(() =>
		initialSelectedIndex(flattened, tabStore.getState().mruHistory[1]),
	);

	const flattenedRef = useRef(flattened);
	flattenedRef.current = flattened;

	const selectedIndexRef = useRef(selectedIndex);
	selectedIndexRef.current = selectedIndex;

	const confirm = useCallback(
		(index: number) => {
			const tab = flattenedRef.current[index];

			if (tab) {
				navigationStore.getState().navigate(Page.BROWSER);
				trpc.tabs.switch.mutate({ tabId: tab.id });
			}

			onClose();
		},
		[onClose],
	);

	useEffect(() => {
		const subscription = trpc.hotkeys.events.subscribe(undefined, {
			onData(event: string) {
				if (event === HotkeyEvent.TAB_SWITCHER_FORWARD) {
					setSelectedIndex((prev) => (prev + 1) % flattenedRef.current.length);
				} else if (event === HotkeyEvent.TAB_SWITCHER_BACKWARD) {
					setSelectedIndex(
						(prev) =>
							(prev - 1 + flattenedRef.current.length) %
							flattenedRef.current.length,
					);
				}
			},
		});

		return () => subscription.unsubscribe();
	}, []);

	useEffect(() => {
		const handleKeyUp = (event: KeyboardEvent) => {
			if (event.key === "Control") {
				confirm(selectedIndexRef.current);
			}
		};

		const handleKeyDown = (event: KeyboardEvent) => {
			if (event.key === "Escape") {
				onClose();
			}
		};

		window.addEventListener("keyup", handleKeyUp);
		window.addEventListener("keydown", handleKeyDown);

		return () => {
			window.removeEventListener("keyup", handleKeyUp);
			window.removeEventListener("keydown", handleKeyDown);
		};
	}, [confirm, onClose]);

	if (flattened.length === 0) {
		onClose();

		return null;
	}

	const groupOffsets: number[] = [];

	let running = 0;

	for (const group of groups) {
		groupOffsets.push(running);
		running += group.tabs.length;
	}

	return (
		<div className="fixed inset-0 flex items-center justify-center">
			{/* biome-ignore lint/a11y: backdrop dismiss, Escape handled via keydown listener */}
			<div className="absolute inset-0 bg-black/40" onClick={onClose} />

			<div className="relative w-[320px] space-y-2 rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl">
				{groups.map((group, groupIndex) => (
					<div key={group.id}>
						<div className="flex items-center gap-2 px-3 pt-1.5 pb-1">
							<div
								className="h-2 w-2 shrink-0 rounded-full"
								style={{
									backgroundColor: PROFILE_COLOR_HEX[group.color],
								}}
							/>

							<span className="truncate font-medium text-[11px] text-white/40">
								{group.name}
							</span>
						</div>

						{group.tabs.map((tab, tabIndex) => {
							const index = groupOffsets[groupIndex] + tabIndex;

							return (
								<button
									type="button"
									key={tab.id}
									className={cn(
										"flex w-full items-center gap-2.5 rounded-lg py-2 pr-3 pl-7 transition-colors",
										index === selectedIndex && "bg-white/10",
									)}
									onMouseDown={() => confirm(index)}
								>
									{tab.favicon ? (
										<img
											src={tab.favicon}
											alt=""
											className="h-4 w-4 shrink-0 rounded-sm"
										/>
									) : (
										<div className="h-4 w-4 shrink-0 rounded-sm bg-white/10" />
									)}

									<span className="truncate text-sm text-white/80">
										{tab.title}
									</span>
								</button>
							);
						})}
					</div>
				))}
			</div>
		</div>
	);
}
```

Key changes from the old file: `resolveMruTabs()` and the `MruTab` interface are gone (moved into the grouping module as `groupMruTabs`/types); the flat list of rows becomes a per-group header (`color dot + name`) plus indented tab rows (`pl-7`, no per-row dot); the per-row `onMouseEnter` is already absent and stays absent; selection indexes the flattened list via `groupOffsets`.

- [ ] **Step 2: Typecheck**

Run: `cd apps/veil && bun run typecheck`
Expected: PASS — no errors. (Confirms `BrowserProfile[]` from the store is structurally assignable to `GroupingProfile[]`, and no dangling references to the removed `resolveMruTabs`/`MruTab`/`ProfileColor` import remain.)

- [ ] **Step 3: Run the full package test suite**

Run: `cd apps/veil && bun run test`
Expected: PASS — existing tests plus the Task 1 grouping tests all green.

- [ ] **Step 4: Commit**

```bash
git add apps/veil/src/renderer/components/tab-switcher.tsx
git commit -m "Group the Ctrl+Tab switcher by profile with name headers"
```

---

### Task 3: Quality gates & manual verification

**Files:** none (verification only).

- [ ] **Step 1: Lint**

Run (from repo root): `bun run lint`
Expected: no errors; Biome may auto-format (e.g. Tailwind class order). If it rewrites files, review and `git commit -m "Apply lint formatting"`.

- [ ] **Step 2: Knip**

Run (from repo root): `bun run knip`
Expected: zero issues — the new module's exports are all consumed (`groupMruTabs`, `initialSelectedIndex` by the component; the interfaces by the module and tests). If knip flags an unused export, remove it.

- [ ] **Step 3: Typecheck the workspace**

Run (from repo root): `bun run typecheck`
Expected: PASS across packages.

- [ ] **Step 4: Manual run**

Run: `cd apps/veil && bun run dev`

Verify in the running app, with at least two profiles each holding a couple of open tabs:
- Hold Ctrl and press Tab: the overlay appears with tabs grouped under profile-name headers; each header shows the profile color dot + name.
- A single Ctrl+Tab (press + release) flips back to the previously-active tab (quick-flick preserved).
- Holding Ctrl and repeatedly pressing Tab walks the flattened order top-to-bottom and wraps; Ctrl+Shift+Tab walks backward.
- The most-recently-used profile's group is at the top.
- Moving the mouse over rows does nothing; clicking a row switches to it.
- Releasing Ctrl confirms the highlighted tab; Escape cancels.

- [ ] **Step 5: Commit any remaining formatting**

If Steps 1–3 produced changes not yet committed:

```bash
git add -A
git commit -m "Apply lint formatting"
```

---

## Self-Review

**Spec coverage:**
- "Grouped by profile, recency order, tabs in MRU order within group" → Task 1 `groupMruTabs` + its tests.
- "8-tab cap counting tabs not headers" → `maxTabs` param + cap test.
- "Flattened selection, wrap, Ctrl+Tab/Shift+Tab" → Task 2 subscription handler + `groupOffsets`.
- "Quick-flick: preselect mruHistory[1], fallback min(1, len-1)" → `initialSelectedIndex` + its tests.
- "Header carries color dot + name, natural case; per-row dot removed; rows indented" → Task 2 render block.
- "Single profile still shows a header; missing-profile tab skipped; empty closes" → header always renders per group; skip covered by the `skips tab ids` test; empty handled by the `flattened.length === 0` early return.
- "No store/IPC/data-model changes" → only component + new sibling module touched.

**Placeholder scan:** none — every step has concrete code or an exact command.

**Type consistency:** `groupMruTabs` / `initialSelectedIndex` signatures and the `SwitcherTab`/`GroupingProfile`/`ProfileGroup` shapes are identical across Task 1's definition, its tests, and Task 2's call sites. The component imports only the two functions (no unused type imports); `group.color` is typed via `ProfileGroup`, so only `PROFILE_COLOR_HEX` is imported from `profile-colors`.
