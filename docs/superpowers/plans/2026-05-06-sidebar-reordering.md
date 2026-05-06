# Sidebar Reordering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add drag-and-drop reordering for profiles and tabs within profiles in the sidebar.

**Architecture:** Two independent `DragDropProvider` scopes — one wrapping the profile list, one per expanded profile's tab list. Store actions mutate array positions; the existing `sync` + `persist` middleware auto-propagates to main process and disk. No new IPC routes.

**Tech Stack:** `@dnd-kit/react` (0.4.0), `@dnd-kit/helpers` (0.4.0), `@dnd-kit/abstract` (transitive, for custom modifier), Zustand, React 19

---

### Task 1: Install @dnd-kit dependencies

**Files:**
- Modify: `apps/veil/package.json`

- [ ] **Step 1: Install packages**

Run from repo root:

```bash
cd apps/veil && pnpm add @dnd-kit/react @dnd-kit/helpers
```

This installs `@dnd-kit/react` (which brings `@dnd-kit/dom`, `@dnd-kit/abstract`, `@dnd-kit/state` as transitive deps) and `@dnd-kit/helpers` (the `move` utility and other helpers).

- [ ] **Step 2: Verify installation**

```bash
pnpm ls @dnd-kit/react @dnd-kit/helpers @dnd-kit/dom @dnd-kit/abstract --filter=@pane/veil
```

Expected: All four packages listed (react and helpers as direct, dom and abstract as transitive).

- [ ] **Step 3: Commit**

```bash
git add apps/veil/package.json pnpm-lock.yaml
git commit -m "feat: add @dnd-kit dependencies for sidebar reordering"
```

---

### Task 2: Add reorder store actions

**Files:**
- Modify: `apps/veil/src/stores/profile-store.ts`

- [ ] **Step 1: Add action types to ProfileState interface**

In `apps/veil/src/stores/profile-store.ts`, add two new actions to the `ProfileState` interface (after the `updateTab` line):

```ts
interface ProfileState {
	profiles: BrowserProfile[];

	create: (input: CreateInput) => string;
	remove: (id: string) => void;
	openTab: (profileId: string, tabId: string, url: string) => void;
	closeTab: (profileId: string, tabId: string) => void;
	updateTab: (profileId: string, tabId: string, partial: Partial<Tab>) => void;
	reorderProfiles: (fromIndex: number, toIndex: number) => void;
	reorderTabs: (profileId: string, fromIndex: number, toIndex: number) => void;
}
```

- [ ] **Step 2: Implement reorderProfiles action**

Add after the `updateTab` implementation (after line 160, before the closing `}`/`)` of the `sync` callback):

```ts
reorderProfiles: (fromIndex, toIndex) => {
	set((state) => {
		const profiles = [...state.profiles];
		const [moved] = profiles.splice(fromIndex, 1);
		profiles.splice(toIndex, 0, moved);

		return { profiles };
	});
},
```

- [ ] **Step 3: Implement reorderTabs action**

Add right after `reorderProfiles`:

```ts
reorderTabs: (profileId, fromIndex, toIndex) => {
	set((state) => ({
		profiles: state.profiles.map((profile) => {
			if (profile.id !== profileId) return profile;

			const tabs = [...profile.tabs];
			const [moved] = tabs.splice(fromIndex, 1);
			tabs.splice(toIndex, 0, moved);

			return { ...profile, tabs };
		}),
	}));
},
```

- [ ] **Step 4: Verify build**

```bash
cd /Users/andrevictor/www/pane && pnpm turbo run typecheck --filter=@pane/veil
```

Expected: No type errors.

- [ ] **Step 5: Commit**

```bash
git add apps/veil/src/stores/profile-store.ts
git commit -m "feat: add reorderProfiles and reorderTabs store actions"
```

---

### Task 3: Create RestrictToVerticalAxis modifier

**Files:**
- Create: `apps/veil/src/renderer/modifiers/restrict-to-vertical-axis.ts`

- [ ] **Step 1: Create the modifier file**

Create `apps/veil/src/renderer/modifiers/restrict-to-vertical-axis.ts`:

```ts
import { Modifier } from "@dnd-kit/abstract";
import type { DragDropManager } from "@dnd-kit/dom";

export class RestrictToVerticalAxis extends Modifier<DragDropManager> {
	apply({ transform }: { transform: { x: number; y: number } }) {
		return { x: 0, y: transform.y };
	}
}
```

- [ ] **Step 2: Verify build**

```bash
cd /Users/andrevictor/www/pane && pnpm turbo run typecheck --filter=@pane/veil
```

Expected: No type errors.

- [ ] **Step 3: Commit**

```bash
git add apps/veil/src/renderer/modifiers/restrict-to-vertical-axis.ts
git commit -m "feat: add RestrictToVerticalAxis modifier for dnd-kit"
```

---

### Task 4: Wire up profile drag-and-drop reordering

**Files:**
- Modify: `apps/veil/src/renderer/app/layout.tsx`

This task wraps the profile list with `DragDropProvider`, makes each `SidebarProfileItem` sortable via the profile header as drag handle, and adds a `DragOverlay` for visual feedback.

- [ ] **Step 1: Add imports to layout.tsx**

Add these imports at the top of `apps/veil/src/renderer/app/layout.tsx`:

```ts
import { DragDropProvider, DragOverlay } from "@dnd-kit/react";
import { useSortable, isSortable } from "@dnd-kit/react/sortable";
import {
	PointerSensor,
	PointerActivationConstraints,
} from "@dnd-kit/dom";
import { RestrictToVerticalAxis } from "../modifiers/restrict-to-vertical-axis";
```

- [ ] **Step 2: Create the shared sensor config**

Add a module-level constant near the top of the file (after imports, before components):

```ts
const SENSORS = [
	PointerSensor.configure({
		activationConstraints: [
			new PointerActivationConstraints.Distance({ value: 5 }),
		],
	}),
];
```

- [ ] **Step 3: Add `index` prop to SidebarProfileItem and wire useSortable**

Update the `SidebarProfileItem` component signature to accept `index`, call `useSortable`, and pass `ref` and `handleRef` down:

```ts
function SidebarProfileItem({
	id,
	index,
	expanded,
	onToggle,
}: {
	id: string;
	index: number;
	expanded: boolean;
	onToggle: (id: string) => void;
}) {
	const profile = useStore(profileStore, (state) =>
		state.profiles.find((profile) => profile.id === id),
	);

	const activeTabId = useStore(tabStore, (state) => state.activeTabId);
	const page = useStore(navigationStore, (state) => state.page);

	const { ref, handleRef, isDragSource } = useSortable({ id, index });

	const handleToggle = useCallback(() => {
		onToggle(id);

		if (!expanded) {
			trpc.profiles.load.mutate({ profileId: id });
		}
	}, [id, expanded, onToggle]);

	if (!profile) {
		return null;
	}

	const isRunning = profile.tabs.length > 0;

	return (
		<ProfileItem ref={ref} style={{ opacity: isDragSource ? 0.4 : 1 }}>
			<ProfileHeader
				ref={handleRef}
				className="cursor-grab"
				color={profile.color}
				active={isRunning}
				onClick={handleToggle}
			>
				<ProfileName>{profile.name}</ProfileName>

				{!expanded && isRunning ? (
					<ProfileBadge>{profile.tabs.length}</ProfileBadge>
				) : null}

				<div className="w-0 shrink-0 transition-[width] duration-150 group-hover:w-3">
					<Trash2
						className="h-3 w-3 translate-x-2 text-muted-foreground opacity-0 transition-[transform,opacity] duration-150 group-hover:translate-x-0 group-hover:opacity-100"
						onClick={(event) => {
							event.stopPropagation();
							trpc.profiles.remove.mutate({ profileId: profile.id });
						}}
					/>
				</div>
			</ProfileHeader>

			{expanded ? (
				<ProfileTabs>
					{profile.tabs.map((tab) => (
						<TabItem
							key={tab.id}
							active={activeTabId === tab.id && page === Page.BROWSER}
							onClick={() => {
								navigationStore.getState().navigate(Page.BROWSER);
								trpc.tabs.switch.mutate({ tabId: tab.id });
							}}
						>
							<TabFavicon src={tab.favicon || undefined} />
							<TabTitle>{tab.title || "Loading..."}</TabTitle>
							<X
								className="h-3 w-3 shrink-0 opacity-0 transition-opacity group-hover:opacity-100"
								onClick={(event) => {
									event.stopPropagation();
									trpc.tabs.close.mutate({ tabId: tab.id });
								}}
							/>
						</TabItem>
					))}
					<TabNew
						onClick={() => {
							navigationStore.getState().navigate(Page.BROWSER);
							trpc.tabs.open.mutate({ profileId: profile.id });
						}}
					/>
				</ProfileTabs>
			) : null}
		</ProfileItem>
	);
}
```

Key changes from the original:
- Added `index` prop
- Added `useSortable({ id, index })` call
- Passed `ref` to `ProfileItem`, `handleRef` to `ProfileHeader`
- Added `style={{ opacity: isDragSource ? 0.4 : 1 }}` on `ProfileItem`
- Added `className="cursor-grab"` on `ProfileHeader`

- [ ] **Step 4: Create ProfileDragOverlay component**

Add this component in `layout.tsx` (before the `Layout` component):

```ts
function ProfileDragOverlay({ profileId }: { profileId: string }) {
	const profile = useStore(profileStore, (state) =>
		state.profiles.find((p) => p.id === profileId),
	);

	if (!profile) return null;

	return (
		<div className="w-[200px] scale-[1.02] cursor-grabbing">
			<ProfileHeader
				className="shadow-lg"
				color={profile.color}
				active={profile.tabs.length > 0}
			>
				<ProfileName>{profile.name}</ProfileName>
			</ProfileHeader>
		</div>
	);
}
```

- [ ] **Step 5: Wrap profile list with DragDropProvider in Layout**

Replace the `<SidebarContent>` section in the `Layout` component (lines 265-274):

**Before:**
```tsx
<SidebarContent>
	{profileIds.map((id) => (
		<SidebarProfileItem
			key={id}
			id={id}
			expanded={expanded.has(id)}
			onToggle={toggleExpanded}
		/>
	))}
</SidebarContent>
```

**After:**
```tsx
<SidebarContent>
	<DragDropProvider
		sensors={SENSORS}
		modifiers={[RestrictToVerticalAxis]}
		onDragEnd={(event) => {
			if (event.canceled) return;

			const { source } = event.operation;

			if (isSortable(source)) {
				if (source.initialIndex !== source.index) {
					profileStore
						.getState()
						.reorderProfiles(source.initialIndex, source.index);
				}
			}
		}}
	>
		{profileIds.map((id, index) => (
			<SidebarProfileItem
				key={id}
				id={id}
				index={index}
				expanded={expanded.has(id)}
				onToggle={toggleExpanded}
			/>
		))}

		<DragOverlay>
			{(source) => (
				<ProfileDragOverlay profileId={String(source.id)} />
			)}
		</DragOverlay>
	</DragDropProvider>
</SidebarContent>
```

- [ ] **Step 6: Verify build**

```bash
cd /Users/andrevictor/www/pane && pnpm turbo run typecheck --filter=@pane/veil
```

Expected: No type errors. If `ProfileItem` or `ProfileHeader` don't accept `ref` cleanly, update their types to use `ComponentPropsWithRef<'div'>` and `ComponentPropsWithRef<'button'>` respectively (see Task 4 Step 7).

- [ ] **Step 7: Fix ref forwarding on presentational components (if needed)**

If Step 6 shows type errors about `ref`, update the presentational components in `apps/veil/src/renderer/components/sidebar/profile-item.tsx`:

For `ProfileItem`, change the props type:

```ts
export function ProfileItem({
	className,
	ref,
	...props
}: React.ComponentPropsWithRef<"div">) {
	return <div ref={ref} className={cn("mb-1.5", className)} {...props} />;
}
```

For `ProfileHeader`, change the props type:

```ts
interface ProfileHeaderProps extends React.ComponentPropsWithRef<"button"> {
	color: ProfileColor;
	active?: boolean;
}

export function ProfileHeader({
	className,
	color,
	active,
	children,
	ref,
	...props
}: ProfileHeaderProps) {
	const hex = PROFILE_COLOR_HEX[color];

	return (
		<button
			ref={ref}
			type="button"
			className={cn(
				"group flex w-full items-center gap-1.5 overflow-hidden rounded-md px-2 py-1.5 text-xs transition-colors",
				active ? "text-[#d4d4d8]" : "text-[#71717a] hover:bg-accent",
				className,
			)}
			style={
				active
					? {
							border: `1px solid color-mix(in srgb, ${hex} 40%, transparent)`,
							background: `color-mix(in srgb, ${hex} 4%, transparent)`,
						}
					: { border: "1px solid transparent" }
			}
			{...props}
		>
			<div
				className="h-3.5 w-1 shrink-0 rounded-sm"
				style={{ background: hex }}
			/>
			{children}
		</button>
	);
}
```

- [ ] **Step 8: Test manually**

```bash
cd /Users/andrevictor/www/pane && pnpm turbo run dev --filter=@pane/veil
```

Verify:
- Profiles can be dragged and reordered by grabbing the profile header
- Drag overlay shows a styled clone of the profile header
- Axis is locked to vertical
- Clicking a profile still toggles expand/collapse
- The reorder persists after restarting the app

- [ ] **Step 9: Commit**

```bash
git add apps/veil/src/renderer/app/layout.tsx apps/veil/src/renderer/components/sidebar/profile-item.tsx
git commit -m "feat: add profile drag-and-drop reordering in sidebar"
```

---

### Task 5: Wire up tab drag-and-drop reordering

**Files:**
- Modify: `apps/veil/src/renderer/app/layout.tsx`
- Modify: `apps/veil/src/renderer/components/sidebar/tab-item.tsx` (if ref forwarding needed)

This task adds a per-profile `DragDropProvider` around each expanded tab list, makes each tab sortable, and adds a `DragOverlay` for tab visual feedback.

- [ ] **Step 1: Create SortableTab component**

Add this component in `layout.tsx` (near `SidebarProfileItem`, before `ProfileDragOverlay`):

```ts
function SortableTab({
	tab,
	profileId,
	index,
}: {
	tab: Tab;
	profileId: string;
	index: number;
}) {
	const activeTabId = useStore(tabStore, (state) => state.activeTabId);
	const page = useStore(navigationStore, (state) => state.page);
	const { ref, isDragSource } = useSortable({ id: tab.id, index });

	return (
		<TabItem
			ref={ref}
			className="cursor-grab"
			active={activeTabId === tab.id && page === Page.BROWSER}
			style={{ opacity: isDragSource ? 0.4 : 1 }}
			onClick={() => {
				navigationStore.getState().navigate(Page.BROWSER);
				trpc.tabs.switch.mutate({ tabId: tab.id });
			}}
		>
			<TabFavicon src={tab.favicon || undefined} />
			<TabTitle>{tab.title || "Loading..."}</TabTitle>
			<X
				className="h-3 w-3 shrink-0 opacity-0 transition-opacity group-hover:opacity-100"
				onClick={(event) => {
					event.stopPropagation();
					trpc.tabs.close.mutate({ tabId: tab.id });
				}}
			/>
		</TabItem>
	);
}
```

Also add the `Tab` type import at the top of the file:

```ts
import { profileStore, type Tab } from "../../stores/profile-store";
```

- [ ] **Step 2: Create TabDragOverlay component**

Add this component in `layout.tsx` (next to `ProfileDragOverlay`):

```ts
function TabDragOverlay({
	tabId,
	profileId,
}: {
	tabId: string;
	profileId: string;
}) {
	const tab = useStore(profileStore, (state) => {
		const profile = state.profiles.find((p) => p.id === profileId);
		return profile?.tabs.find((t) => t.id === tabId);
	});

	if (!tab) return null;

	return (
		<div className="w-[200px] scale-[1.02] cursor-grabbing">
			<div className="flex w-full items-center gap-1.5 rounded-[5px] bg-[rgba(255,255,255,0.05)] px-2 py-1 text-[11px] text-[#e4e4e7] shadow-lg">
				<TabFavicon src={tab.favicon || undefined} />
				<TabTitle>{tab.title || "Loading..."}</TabTitle>
			</div>
		</div>
	);
}
```

- [ ] **Step 3: Replace inline tab list with sortable tab list**

In `SidebarProfileItem`, replace the expanded section (the `{expanded ? (...) : null}` block):

**Before:**
```tsx
{expanded ? (
	<ProfileTabs>
		{profile.tabs.map((tab) => (
			<TabItem
				key={tab.id}
				active={activeTabId === tab.id && page === Page.BROWSER}
				onClick={() => {
					navigationStore.getState().navigate(Page.BROWSER);
					trpc.tabs.switch.mutate({ tabId: tab.id });
				}}
			>
				<TabFavicon src={tab.favicon || undefined} />
				<TabTitle>{tab.title || "Loading..."}</TabTitle>
				<X
					className="h-3 w-3 shrink-0 opacity-0 transition-opacity group-hover:opacity-100"
					onClick={(event) => {
						event.stopPropagation();
						trpc.tabs.close.mutate({ tabId: tab.id });
					}}
				/>
			</TabItem>
		))}
		<TabNew
			onClick={() => {
				navigationStore.getState().navigate(Page.BROWSER);
				trpc.tabs.open.mutate({ profileId: profile.id });
			}}
		/>
	</ProfileTabs>
) : null}
```

**After:**
```tsx
{expanded ? (
	<DragDropProvider
		sensors={SENSORS}
		modifiers={[RestrictToVerticalAxis]}
		onDragEnd={(event) => {
			if (event.canceled) return;

			const { source } = event.operation;

			if (isSortable(source)) {
				if (source.initialIndex !== source.index) {
					profileStore
						.getState()
						.reorderTabs(
							profile.id,
							source.initialIndex,
							source.index,
						);
				}
			}
		}}
	>
		<ProfileTabs>
			{profile.tabs.map((tab, index) => (
				<SortableTab
					key={tab.id}
					tab={tab}
					profileId={profile.id}
					index={index}
				/>
			))}
			<TabNew
				onClick={() => {
					navigationStore.getState().navigate(Page.BROWSER);
					trpc.tabs.open.mutate({ profileId: profile.id });
				}}
			/>
		</ProfileTabs>

		<DragOverlay>
			{(source) => (
				<TabDragOverlay
					tabId={String(source.id)}
					profileId={profile.id}
				/>
			)}
		</DragOverlay>
	</DragDropProvider>
) : null}
```

- [ ] **Step 4: Fix ref forwarding on TabItem (if needed)**

If typecheck fails on `ref` prop for `TabItem`, update `apps/veil/src/renderer/components/sidebar/tab-item.tsx`:

```ts
interface TabItemProps extends React.ComponentPropsWithRef<"button"> {
	active?: boolean;
}

export function TabItem({ className, active, ref, ...props }: TabItemProps) {
	return (
		<button
			ref={ref}
			type="button"
			className={cn(
				"group flex w-full items-center gap-1.5 rounded-[5px] px-2 py-1 text-[11px] transition-colors",
				active
					? "bg-[rgba(255,255,255,0.05)] text-[#e4e4e7]"
					: "text-[#71717a] hover:bg-accent hover:text-accent-foreground",
				className,
			)}
			{...props}
		/>
	);
}
```

- [ ] **Step 5: Remove unused imports**

After refactoring, `SidebarProfileItem` no longer directly uses `activeTabId` and `page` for rendering tabs (that moved to `SortableTab`). Remove the now-unused store reads from `SidebarProfileItem`:

```ts
// Remove these lines from SidebarProfileItem:
const activeTabId = useStore(tabStore, (state) => state.activeTabId);
const page = useStore(navigationStore, (state) => state.page);
```

Note: Keep the `page` import at the file level since `Layout` still uses it. Only remove the lines from `SidebarProfileItem` specifically. `TabItem` is still used by `SortableTab`, so keep it in the imports.

- [ ] **Step 6: Verify build**

```bash
cd /Users/andrevictor/www/pane && pnpm turbo run typecheck --filter=@pane/veil
```

Expected: No type errors.

- [ ] **Step 7: Test manually**

```bash
cd /Users/andrevictor/www/pane && pnpm turbo run dev --filter=@pane/veil
```

Verify:
- Tabs within an expanded profile can be dragged and reordered
- Drag overlay shows a styled clone of the tab
- Axis is locked to vertical
- Clicking a tab still switches to it
- Dragging a tab does NOT trigger profile reordering
- Dragging a profile header still works as before
- Tab close button still works
- "New tab" button still works
- Tab order persists after restarting the app

- [ ] **Step 8: Commit**

```bash
git add apps/veil/src/renderer/app/layout.tsx apps/veil/src/renderer/components/sidebar/tab-item.tsx
git commit -m "feat: add tab drag-and-drop reordering within profiles"
```
