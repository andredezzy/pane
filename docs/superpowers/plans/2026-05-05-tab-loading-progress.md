# Tab Loading Progress Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a simulated loading progress bar inside the address input and swap the reload button for a stop button while a tab is loading.

**Architecture:** The main process listens to Electron's `did-start-loading` / `did-stop-loading` events on each tab's `webContents` and pushes loading state into `tabStore`. The renderer reads this state to drive a CSS-animated progress bar inside the address input and toggle the reload/stop icon.

**Tech Stack:** Electron WebContents events, Zustand store with sync middleware, React + Tailwind CSS transitions, lucide-react icons, tRPC mutations.

---

### Task 1: Add loading state to tab store

**Files:**
- Modify: `apps/desktop/src/stores/tab-store.ts`

- [ ] **Step 1: Add `loadingTabIds` state and `setLoading` action**

```ts
// apps/desktop/src/stores/tab-store.ts
import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export interface TabState {
	activeTabId: string | null;
	activeProfileId: string | null;
	loadingTabIds: string[];

	setActiveTab: (tabId: string | null, profileId: string | null) => void;
	setLoading: (tabId: string, isLoading: boolean) => void;
}

export const tabStore = createStore<TabState>()(
	sync(
		(set) => ({
			activeTabId: null,
			activeProfileId: null,
			loadingTabIds: [],

			setActiveTab: (tabId, profileId) =>
				set({ activeTabId: tabId, activeProfileId: profileId }),

			setLoading: (tabId, isLoading) =>
				set((state) => ({
					loadingTabIds: isLoading
						? state.loadingTabIds.includes(tabId)
							? state.loadingTabIds
							: [...state.loadingTabIds, tabId]
						: state.loadingTabIds.filter((id) => id !== tabId),
				})),
		}),
		{ name: "tab-store" },
	),
);
```

- [ ] **Step 2: Verify typecheck passes**

Run: `npx turbo run typecheck --filter=@pane/desktop`
Expected: all tasks successful

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/tab-store.ts
git commit -m "feat: add loading state to tab store"
```

---

### Task 2: Emit loading events from main process

**Files:**
- Modify: `apps/desktop/src/main/profile/profile-tabs.ts`

- [ ] **Step 1: Add `did-start-loading` and `did-stop-loading` listeners in `createView`**

In the `createView` method, after the existing `page-favicon-updated` listener (around line 239), add:

```ts
view.webContents.on("did-start-loading", () => {
	tabStore.getState().setLoading(tabId, true);
});

view.webContents.on("did-stop-loading", () => {
	tabStore.getState().setLoading(tabId, false);
});
```

- [ ] **Step 2: Clean up loading state in `close` method**

In the `close` method, after `this.views.delete(tabId)` (line 62), add:

```ts
tabStore.getState().setLoading(tabId, false);
```

This ensures loading state is cleared when a tab is closed mid-load.

- [ ] **Step 3: Add `stop` method to `ProfileTabs`**

After the `reload()` method (line 141), add:

```ts
stop(): void {
	this.activeView()?.webContents.stop();
}
```

- [ ] **Step 4: Verify typecheck passes**

Run: `npx turbo run typecheck --filter=@pane/desktop`
Expected: all tasks successful

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/main/profile/profile-tabs.ts
git commit -m "feat: emit tab loading events and add stop method"
```

---

### Task 3: Add `tabs.stop` tRPC procedure

**Files:**
- Modify: `apps/desktop/src/main/trpc/routers/tabs.ts`

- [ ] **Step 1: Add `stop` procedure**

After the `reload` procedure (line 49), add:

```ts
stop: procedure.mutation(({ ctx }) => {
	findActiveProfile(ctx)?.tabs.stop();
}),
```

- [ ] **Step 2: Verify typecheck passes**

Run: `npx turbo run typecheck --filter=@pane/desktop`
Expected: all tasks successful

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/trpc/routers/tabs.ts
git commit -m "feat: add tabs.stop tRPC procedure"
```

---

### Task 4: Update toolbar components — progress bar and stop button

**Files:**
- Modify: `apps/desktop/src/renderer/pages/browser/_components/toolbar.tsx`

- [ ] **Step 1: Remove border from `ToolbarAddress` and wrap with progress bar container**

Replace the existing `ToolbarAddress` component with a container that holds the input and a progress bar:

```tsx
export const ToolbarAddress = forwardRef<
	HTMLInputElement,
	InputHTMLAttributes<HTMLInputElement> & { loading?: boolean }
>(({ className, loading, ...props }, ref) => (
	<div className="relative flex flex-1 overflow-hidden rounded-[5px]">
		<input
			ref={ref}
			type="text"
			className={cn(
				"h-[30px] flex-1 bg-[rgba(255,255,255,0.03)] px-2.5 text-foreground text-xs placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-ring rounded-[5px]",
				className,
			)}
			{...props}
		/>
		<ToolbarAddressProgress loading={!!loading} />
	</div>
));

ToolbarAddress.displayName = "ToolbarAddress";
```

The progress component is always mounted but returns `null` when idle. When `loading` transitions `false → true`, it enters the "growing" phase. When `loading` transitions `true → false`, it enters "completing" (snap to 100% + fade out), then resets to idle.

- [ ] **Step 2: Create the `ToolbarAddressProgress` component**

Add this component above `ToolbarAddress` in the same file. The `loading` prop stays `true` during loading and becomes `false` when done — the component handles the completion animation internally before calling `onDone`:

```tsx
type ProgressPhase = "idle" | "growing" | "completing";

function ToolbarAddressProgress({ loading }: { loading: boolean }) {
	const [phase, setPhase] = useState<ProgressPhase>("idle");

	useEffect(() => {
		if (loading && phase === "idle") {
			setPhase("growing");
		} else if (!loading && phase === "growing") {
			setPhase("completing");
		}
	}, [loading, phase]);

	if (phase === "idle") {
		return null;
	}

	return (
		<div
			className="absolute bottom-0 left-0 h-[2px] bg-gradient-to-r from-blue-500 to-blue-400"
			style={{
				width: phase === "growing" ? "90%" : "100%",
				opacity: phase === "completing" ? 0 : 1,
				transition:
					phase === "growing"
						? "width 8s cubic-bezier(0.1, 0.05, 0, 1)"
						: "width 200ms ease-out, opacity 200ms ease-out 200ms",
			}}
			onTransitionEnd={(e) => {
				if (e.propertyName === "opacity" && phase === "completing") {
					setPhase("idle");
				}
			}}
		/>
	);
}
```

The animation phases:
1. `idle` — returns null, no bar visible
2. `growing` — triggered when `loading` becomes true; width animates from 0% to 90% over 8s with a front-loaded cubic bezier (reaches ~80% in ~2s, then crawls)
3. `completing` — triggered when `loading` becomes false; width snaps to 100% over 200ms, then opacity fades to 0 over 200ms, then resets to `idle`

Add the necessary imports at the top of the file — add `useState` and `useEffect`:

```ts
import {
	type ButtonHTMLAttributes,
	Children,
	forwardRef,
	type HTMLAttributes,
	type InputHTMLAttributes,
	type PropsWithChildren,
	useEffect,
	useState,
} from "react";
```

- [ ] **Step 3: Update `ToolbarNavigationReload` to support loading state**

Replace the existing `ToolbarNavigationReload` with a component that toggles between reload and stop:

```tsx
export function ToolbarNavigationReload({
	className,
	loading,
	...props
}: ButtonHTMLAttributes<HTMLButtonElement> & { loading?: boolean }) {
	return (
		<button type="button" className={cn(navBtnClass, className)} {...props}>
			{loading ? (
				<X className="h-3 w-3" />
			) : (
				<RotateCw className="h-3 w-3" />
			)}
		</button>
	);
}
```

Add `X` to the lucide-react import at the top:

```ts
import { ArrowLeft, ArrowRight, RotateCw, X } from "lucide-react";
```

- [ ] **Step 4: Verify typecheck passes**

Run: `npx turbo run typecheck --filter=@pane/desktop`
Expected: all tasks successful

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/renderer/pages/browser/_components/toolbar.tsx
git commit -m "feat: add progress bar to address input and toggle reload/stop"
```

---

### Task 5: Wire loading state into BrowserToolbar

**Files:**
- Modify: `apps/desktop/src/renderer/pages/browser/index.tsx`

- [ ] **Step 1: Read loading state and pass to toolbar components**

Update `BrowserToolbar` to read `loadingTabIds` from `tabStore` and derive an `isLoading` boolean. Then pass it to `ToolbarAddress` and `ToolbarNavigationReload`, and switch the reload click handler.

Replace the existing `BrowserToolbar` function with:

```tsx
function BrowserToolbar() {
	const { activeTabId, activeProfileId } = useStore(
		tabStore,
		useShallow((s) => ({
			activeTabId: s.activeTabId,
			activeProfileId: s.activeProfileId,
		})),
	);

	const loadingTabIds = useStore(tabStore, (s) => s.loadingTabIds);

	const profiles = useStore(profileStore, (s) => s.profiles);
	const extensions = useStore(extensionStore, (s) => s.extensions);

	const activeProfile = profiles.find((p) => p.id === activeProfileId);
	const activeTab = activeProfile?.tabs.find((t) => t.id === activeTabId);

	const activeUrl = activeTab?.url ?? "";
	const profileName = activeProfile?.name ?? "";

	const profileColor: ProfileColorType =
		activeProfile?.color ?? ProfileColor.BLUE;

	const profileExtensions = activeProfileId
		? extensions[activeProfileId]
		: undefined;

	const [inputValue, setInputValue] = useState("");
	const [isFocused, setIsFocused] = useState(false);

	const displayUrl = isFocused ? inputValue : activeUrl;

	const isLoading = activeTabId
		? loadingTabIds.includes(activeTabId)
		: false;

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();

		if (inputValue.trim()) {
			trpc.tabs.navigate.mutate({ url: inputValue.trim() });
			(document.activeElement as HTMLElement)?.blur();
		}
	};

	if (!activeTabId) {
		return null;
	}

	return (
		<Toolbar>
			<ToolbarNavigation>
				<ToolbarNavigationBack onClick={() => trpc.tabs.goBack.mutate()} />
				<ToolbarNavigationForward
					onClick={() => trpc.tabs.goForward.mutate()}
				/>
				<ToolbarNavigationReload
					loading={isLoading}
					onClick={() =>
						isLoading
							? trpc.tabs.stop.mutate()
							: trpc.tabs.reload.mutate()
					}
				/>
			</ToolbarNavigation>

			<form onSubmit={handleSubmit} className="flex flex-1">
				<ToolbarAddress
					loading={isLoading}
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

			<ToolbarExtensions>
				{profileExtensions && profileExtensions.length > 0 && (
					<BrowserActionList partition={`persist:profile-${activeProfileId}`} />
				)}
			</ToolbarExtensions>

			{profileName ? (
				<ToolbarProfile color={profileColor}>{profileName}</ToolbarProfile>
			) : null}
		</Toolbar>
	);
}
```

- [ ] **Step 2: Verify typecheck passes**

Run: `npx turbo run typecheck --filter=@pane/desktop`
Expected: all tasks successful

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/pages/browser/index.tsx
git commit -m "feat: wire loading state into browser toolbar"
```

---

### Task 6: Manual test and final commit

- [ ] **Step 1: Start the dev server**

Run: `npx turbo run dev --filter=@pane/desktop`

- [ ] **Step 2: Test the loading indicator**

1. Open a profile and open a tab
2. Navigate to a URL — verify the blue progress bar appears at the bottom of the address input and animates
3. Verify the reload button (↻) becomes a stop button (✕) during loading
4. Click the stop button — verify the page stops loading and the button reverts to reload
5. Navigate to a fast-loading page — verify the progress bar completes and fades out
6. Close a tab while it's loading — verify no stale loading state

- [ ] **Step 3: Squash or leave commits as-is based on preference**
