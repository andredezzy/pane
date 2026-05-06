# Profile Context Menu + Unified Sheet — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add right-click context menu on sidebar profiles (edit + delete), refactor the creation sheet into a unified create/edit sheet, and extend the surface system with type-safe prop passing.

**Architecture:** The surface system (separate Electron window for sheets) gets extended to pass serializable props alongside the component name — fully typed via generics. The profile store gets an `update` method. The creation sheet becomes a unified `ProfileSheet` that reads an optional `profileId` prop to switch between create/edit mode. A Radix context menu primitive is added to `@pane/ui` and wired into the sidebar profile items.

**Tech Stack:** React 19, Zustand, Radix UI (`@radix-ui/react-context-menu`), react-hook-form + zod/v4, tRPC, Electron

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `apps/veil/src/stores/profile-store.ts` | Modify | Remove `notes`/`tags`, add `update` method |
| `apps/veil/src/renderer/surface.ts` | Modify | Type-safe `open` with generic prop inference |
| `apps/veil/src/renderer/app/surface.tsx` | Modify | Pass props from message to rendered component |
| `apps/veil/src/main/trpc/routers/ui.ts` | Modify | Accept optional `props` in `present` mutation |
| `packages/ui/src/components/context-menu.tsx` | Create | Radix context menu primitive (shadcn pattern) |
| `packages/ui/package.json` | Modify | Add `@radix-ui/react-context-menu` dependency |
| `apps/veil/src/renderer/sheets/create-profile.tsx` → `profile-sheet.tsx` | Rename + Modify | Unified create/edit sheet |
| `apps/veil/src/renderer/app/layout.tsx` | Modify | Context menu on profiles, remove trash icon, update imports |

---

### Task 1: YAGNI Cleanup — Remove `notes` and `tags` from Profile Model

**Files:**
- Modify: `apps/veil/src/stores/profile-store.ts:51-68`
- Modify: `apps/veil/src/renderer/sheets/create-profile.tsx:104-126`

- [ ] **Step 1: Remove `notes` and `tags` from `BrowserProfile` interface**

In `apps/veil/src/stores/profile-store.ts`, change the `BrowserProfile` interface from:

```typescript
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
	createdAt: string;
	updatedAt: string;
}
```

To:

```typescript
export interface BrowserProfile {
	id: string;
	name: string;
	color: ProfileColor;
	group: string | null;
	fingerprint: Fingerprint;
	proxy: ProxyConfig | null;
	tabs: Tab[];
	createdAt: string;
	updatedAt: string;
}
```

- [ ] **Step 2: Remove `notes` and `tags` from the `onSubmit` handler in the creation sheet**

In `apps/veil/src/renderer/sheets/create-profile.tsx`, change the `onSubmit` function from:

```typescript
	const onSubmit = (data: FormValues) => {
		profileStore.getState().create({
			name: data.name,
			color: data.color,
			group: data.group || null,
			notes: null,
			fingerprint: DEFAULT_FINGERPRINTS[data.platform],
			proxy:
				data.proxyEnabled && data.proxy?.host
					? {
							proxyType: data.proxy.proxyType,
							host: data.proxy.host,
							port: data.proxy.port,
							username: data.proxy.username || null,
							password: data.proxy.password || null,
						}
					: null,
			tags: [],
		});
```

To:

```typescript
	const onSubmit = (data: FormValues) => {
		profileStore.getState().create({
			name: data.name,
			color: data.color,
			group: data.group || null,
			fingerprint: DEFAULT_FINGERPRINTS[data.platform],
			proxy:
				data.proxyEnabled && data.proxy?.host
					? {
							proxyType: data.proxy.proxyType,
							host: data.proxy.host,
							port: data.proxy.port,
							username: data.proxy.username || null,
							password: data.proxy.password || null,
						}
					: null,
		});
```

- [ ] **Step 3: Verify typecheck passes**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/veil`

Expected: Clean pass (no errors). The `CreateInput` type derives from `BrowserProfile` via `Omit`, so it automatically loses `notes` and `tags`.

- [ ] **Step 4: Commit**

```bash
git add apps/veil/src/stores/profile-store.ts apps/veil/src/renderer/sheets/create-profile.tsx
git commit -m "refactor: remove unused notes and tags fields from profile model"
```

---

### Task 2: Add `update` Method to Profile Store

**Files:**
- Modify: `apps/veil/src/stores/profile-store.ts:70-80` (interface)
- Modify: `apps/veil/src/stores/profile-store.ts:107` (after `remove` method)

- [ ] **Step 1: Add `update` to the `ProfileState` interface**

In `apps/veil/src/stores/profile-store.ts`, change the interface from:

```typescript
interface ProfileState {
	profiles: BrowserProfile[];

	create: (input: CreateInput) => string;
	remove: (id: string) => void;
```

To:

```typescript
interface ProfileState {
	profiles: BrowserProfile[];

	create: (input: CreateInput) => string;
	update: (id: string, input: Partial<CreateInput>) => void;
	remove: (id: string) => void;
```

- [ ] **Step 2: Implement the `update` method in the store**

In the store creator function, add the `update` method right after the `create` method (after the closing of `create`'s function body and before `remove`):

```typescript
				update: (id, input) => {
					set((state) => ({
						profiles: state.profiles.map((profile) =>
							profile.id === id
								? {
										...profile,
										...input,
										updatedAt: new Date().toISOString(),
									}
								: profile,
						),
					}));
				},
```

- [ ] **Step 3: Verify typecheck passes**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/veil`

Expected: Clean pass.

- [ ] **Step 4: Commit**

```bash
git add apps/veil/src/stores/profile-store.ts
git commit -m "feat: add update method to profile store"
```

---

### Task 3: Type-safe Surface System

**Files:**
- Modify: `apps/veil/src/renderer/surface.ts`
- Modify: `apps/veil/src/main/trpc/routers/ui.ts`
- Modify: `apps/veil/src/renderer/app/surface.tsx`

- [ ] **Step 1: Extend `surface.ts` with generic prop inference**

Replace the entire contents of `apps/veil/src/renderer/surface.ts` with:

```typescript
import type { ComponentProps, ComponentType } from "react";
import { trpc } from "./trpc";

type SurfaceProps<C extends ComponentType<any>> = Omit<
	ComponentProps<C>,
	"onClose"
>;

type SurfaceArgs<C extends ComponentType<any>> =
	keyof SurfaceProps<C> extends never
		? []
		: Partial<SurfaceProps<C>> extends SurfaceProps<C>
			? [props?: SurfaceProps<C>]
			: [props: SurfaceProps<C>];

// biome-ignore lint/suspicious/noExplicitAny: accepts any component
type AnyComponent = ComponentType<any>;

export const surface = {
	open<C extends AnyComponent>(component: C, ...args: SurfaceArgs<C>) {
		const [props] = args;

		trpc.ui.present.mutate({
			name: component.name,
			...(props ? { props: props as Record<string, unknown> } : {}),
		});
	},

	close() {
		trpc.ui.dismiss.mutate();
	},
};
```

- [ ] **Step 2: Extend the tRPC `present` mutation to accept props**

Replace the entire contents of `apps/veil/src/main/trpc/routers/ui.ts` with:

```typescript
import { z } from "zod/v4";
import { procedure, router } from "../trpc";

export const uiRouter = router({
	present: procedure
		.input(
			z.object({
				name: z.string(),
				props: z.record(z.string(), z.unknown()).optional(),
			}),
		)
		.mutation(({ input, ctx }) => {
			ctx.surface.show();

			ctx.surface.webContents.executeJavaScript(
				`window.postMessage(${JSON.stringify({ name: input.name, props: input.props })})`,
			);
		}),

	dismiss: procedure.mutation(({ ctx }) => {
		ctx.surface.hide();
	}),
});
```

- [ ] **Step 3: Update `SurfaceLayout` to pass props to rendered components**

Replace the entire contents of `apps/veil/src/renderer/app/surface.tsx` with:

```typescript
import { type ComponentType, useEffect, useRef, useState } from "react";
import { surface } from "../surface";

// biome-ignore lint/suspicious/noExplicitAny: registry accepts any component
type AnyComponent = ComponentType<any>;

const modules = (
	import.meta as unknown as {
		glob: (p: string[], o: object) => Record<string, Record<string, unknown>>;
	}
).glob(["../**/*.tsx"], { eager: true });

const registry = new Map<string, AnyComponent>();
for (const module of Object.values(modules)) {
	for (const value of Object.values(module)) {
		if (typeof value === "function" && value.name) {
			registry.set(value.name, value as AnyComponent);
		}
	}
}

interface ActiveSurface {
	name: string;
	key: number;
	props?: Record<string, unknown>;
}

export function SurfaceLayout() {
	const [active, setActive] = useState<ActiveSurface | null>(null);
	const keyRef = useRef(0);

	useEffect(() => {
		const handle = (e: MessageEvent) => {
			if (typeof e.data?.name === "string" && registry.has(e.data.name)) {
				keyRef.current++;

				setActive({
					name: e.data.name,
					key: keyRef.current,
					props: e.data.props,
				});
			}
		};

		window.addEventListener("message", handle);

		return () => window.removeEventListener("message", handle);
	}, []);

	if (!active) {
		return null;
	}

	const Component = registry.get(active.name);

	if (!Component) {
		return null;
	}

	return (
		<Component key={active.key} {...active.props} onClose={surface.close} />
	);
}
```

- [ ] **Step 4: Verify typecheck passes**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/veil`

Expected: Clean pass.

- [ ] **Step 5: Commit**

```bash
git add apps/veil/src/renderer/surface.ts apps/veil/src/main/trpc/routers/ui.ts apps/veil/src/renderer/app/surface.tsx
git commit -m "feat: extend surface system with type-safe prop passing"
```

---

### Task 4: Context Menu Primitive in `@pane/ui`

**Files:**
- Create: `packages/ui/src/components/context-menu.tsx`
- Modify: `packages/ui/package.json`

- [ ] **Step 1: Install `@radix-ui/react-context-menu` in `@pane/ui`**

Run: `cd /Users/andrevictor/www/pane/packages/ui && bun add @radix-ui/react-context-menu`

- [ ] **Step 2: Create the context menu component**

Create `packages/ui/src/components/context-menu.tsx`:

```typescript
import * as ContextMenuPrimitive from "@radix-ui/react-context-menu";
import type { ComponentPropsWithoutRef, ElementRef } from "react";
import { forwardRef } from "react";

import { cn } from "../cn";

const ContextMenu = ContextMenuPrimitive.Root;
const ContextMenuTrigger = ContextMenuPrimitive.Trigger;
const ContextMenuGroup = ContextMenuPrimitive.Group;
const ContextMenuPortal = ContextMenuPrimitive.Portal;
const ContextMenuSub = ContextMenuPrimitive.Sub;
const ContextMenuRadioGroup = ContextMenuPrimitive.RadioGroup;

const ContextMenuSubTrigger = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.SubTrigger>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.SubTrigger> & {
		inset?: boolean;
	}
>(({ className, inset, children, ...props }, ref) => (
	<ContextMenuPrimitive.SubTrigger
		ref={ref}
		className={cn(
			"flex cursor-default select-none items-center rounded-sm px-2 py-1.5 text-sm outline-none focus:bg-accent focus:text-accent-foreground data-[state=open]:bg-accent data-[state=open]:text-accent-foreground",
			inset && "pl-8",
			className,
		)}
		{...props}
	>
		{children}
	</ContextMenuPrimitive.SubTrigger>
));

ContextMenuSubTrigger.displayName = ContextMenuPrimitive.SubTrigger.displayName;

const ContextMenuSubContent = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.SubContent>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.SubContent>
>(({ className, ...props }, ref) => (
	<ContextMenuPrimitive.SubContent
		ref={ref}
		className={cn(
			"data-[state=closed]:fade-out-0 data-[state=open]:fade-in-0 data-[state=closed]:zoom-out-95 data-[state=open]:zoom-in-95 data-[side=bottom]:slide-in-from-top-2 data-[side=left]:slide-in-from-right-2 data-[side=right]:slide-in-from-left-2 data-[side=top]:slide-in-from-bottom-2 z-50 min-w-[8rem] overflow-hidden rounded-md border border-border bg-popover p-1 text-popover-foreground shadow-lg data-[state=closed]:animate-out data-[state=open]:animate-in",
			className,
		)}
		{...props}
	/>
));

ContextMenuSubContent.displayName = ContextMenuPrimitive.SubContent.displayName;

const ContextMenuContent = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.Content>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.Content>
>(({ className, ...props }, ref) => (
	<ContextMenuPrimitive.Portal>
		<ContextMenuPrimitive.Content
			ref={ref}
			className={cn(
				"data-[state=closed]:fade-out-0 data-[state=open]:fade-in-0 data-[state=closed]:zoom-out-95 data-[state=open]:zoom-in-95 data-[side=bottom]:slide-in-from-top-2 data-[side=left]:slide-in-from-right-2 data-[side=right]:slide-in-from-left-2 data-[side=top]:slide-in-from-bottom-2 z-50 min-w-[8rem] overflow-hidden rounded-md border border-border bg-popover p-1 text-popover-foreground shadow-md data-[state=closed]:animate-out data-[state=open]:animate-in",
				className,
			)}
			{...props}
		/>
	</ContextMenuPrimitive.Portal>
));

ContextMenuContent.displayName = ContextMenuPrimitive.Content.displayName;

const ContextMenuItem = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.Item>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.Item> & {
		inset?: boolean;
	}
>(({ className, inset, ...props }, ref) => (
	<ContextMenuPrimitive.Item
		ref={ref}
		className={cn(
			"relative flex cursor-default select-none items-center rounded-sm px-2 py-1.5 text-sm outline-none focus:bg-accent focus:text-accent-foreground data-[disabled]:pointer-events-none data-[disabled]:opacity-50",
			inset && "pl-8",
			className,
		)}
		{...props}
	/>
));

ContextMenuItem.displayName = ContextMenuPrimitive.Item.displayName;

const ContextMenuCheckboxItem = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.CheckboxItem>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.CheckboxItem>
>(({ className, children, checked, ...props }, ref) => (
	<ContextMenuPrimitive.CheckboxItem
		ref={ref}
		className={cn(
			"relative flex cursor-default select-none items-center rounded-sm py-1.5 pr-2 pl-8 text-sm outline-none focus:bg-accent focus:text-accent-foreground data-[disabled]:pointer-events-none data-[disabled]:opacity-50",
			className,
		)}
		checked={checked}
		{...props}
	>
		<span className="absolute left-2 flex h-3.5 w-3.5 items-center justify-center">
			<ContextMenuPrimitive.ItemIndicator>
				<span className="h-4 w-4">✓</span>
			</ContextMenuPrimitive.ItemIndicator>
		</span>
		{children}
	</ContextMenuPrimitive.CheckboxItem>
));

ContextMenuCheckboxItem.displayName =
	ContextMenuPrimitive.CheckboxItem.displayName;

const ContextMenuRadioItem = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.RadioItem>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.RadioItem>
>(({ className, children, ...props }, ref) => (
	<ContextMenuPrimitive.RadioItem
		ref={ref}
		className={cn(
			"relative flex cursor-default select-none items-center rounded-sm py-1.5 pr-2 pl-8 text-sm outline-none focus:bg-accent focus:text-accent-foreground data-[disabled]:pointer-events-none data-[disabled]:opacity-50",
			className,
		)}
		{...props}
	>
		<span className="absolute left-2 flex h-3.5 w-3.5 items-center justify-center">
			<ContextMenuPrimitive.ItemIndicator>
				<span className="h-2 w-2 rounded-full bg-current" />
			</ContextMenuPrimitive.ItemIndicator>
		</span>
		{children}
	</ContextMenuPrimitive.RadioItem>
));

ContextMenuRadioItem.displayName = ContextMenuPrimitive.RadioItem.displayName;

const ContextMenuLabel = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.Label>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.Label> & {
		inset?: boolean;
	}
>(({ className, inset, ...props }, ref) => (
	<ContextMenuPrimitive.Label
		ref={ref}
		className={cn(
			"px-2 py-1.5 font-semibold text-foreground text-sm",
			inset && "pl-8",
			className,
		)}
		{...props}
	/>
));

ContextMenuLabel.displayName = ContextMenuPrimitive.Label.displayName;

const ContextMenuSeparator = forwardRef<
	ElementRef<typeof ContextMenuPrimitive.Separator>,
	ComponentPropsWithoutRef<typeof ContextMenuPrimitive.Separator>
>(({ className, ...props }, ref) => (
	<ContextMenuPrimitive.Separator
		ref={ref}
		className={cn("-mx-1 my-1 h-px bg-border", className)}
		{...props}
	/>
));

ContextMenuSeparator.displayName = ContextMenuPrimitive.Separator.displayName;

const ContextMenuShortcut = ({
	className,
	...props
}: React.HTMLAttributes<HTMLSpanElement>) => (
	<span
		className={cn(
			"ml-auto text-xs tracking-widest text-muted-foreground",
			className,
		)}
		{...props}
	/>
);

ContextMenuShortcut.displayName = "ContextMenuShortcut";

export {
	ContextMenu,
	ContextMenuCheckboxItem,
	ContextMenuContent,
	ContextMenuGroup,
	ContextMenuItem,
	ContextMenuLabel,
	ContextMenuPortal,
	ContextMenuRadioGroup,
	ContextMenuRadioItem,
	ContextMenuSeparator,
	ContextMenuShortcut,
	ContextMenuSub,
	ContextMenuSubContent,
	ContextMenuSubTrigger,
	ContextMenuTrigger,
};
```

- [ ] **Step 3: Verify typecheck passes**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/ui`

Expected: Clean pass.

- [ ] **Step 4: Commit**

```bash
git add packages/ui/src/components/context-menu.tsx packages/ui/package.json
git commit -m "feat: add context menu primitive to @pane/ui"
```

Note: also `git add` the lockfile if it changed (e.g. `bun.lock`).

---

### Task 5: Unified `ProfileSheet` (Rename + Refactor)

**Files:**
- Rename: `apps/veil/src/renderer/sheets/create-profile.tsx` → `apps/veil/src/renderer/sheets/profile-sheet.tsx`

- [ ] **Step 1: Rename the file**

Run: `cd /Users/andrevictor/www/pane && git mv apps/veil/src/renderer/sheets/create-profile.tsx apps/veil/src/renderer/sheets/profile-sheet.tsx`

- [ ] **Step 2: Replace the entire file with the unified sheet**

Write the following to `apps/veil/src/renderer/sheets/profile-sheet.tsx`:

```typescript
import { zodResolver } from "@hookform/resolvers/zod";
import { Button } from "@pane/ui/components/button";
import {
	Form,
	FormControl,
	FormField,
	FormItem,
	FormLabel,
	FormMessage,
} from "@pane/ui/components/form";
import { Input } from "@pane/ui/components/input";
import {
	Select,
	SelectContent,
	SelectItem,
	SelectTrigger,
	SelectValue,
} from "@pane/ui/components/select";
import { Separator } from "@pane/ui/components/separator";
import {
	Sheet,
	SheetBody,
	SheetContent,
	SheetFooter,
	SheetHeader,
	SheetTitle,
} from "@pane/ui/components/sheet";
import { Switch } from "@pane/ui/components/switch";
import { X } from "lucide-react";
import { useEffect, useState } from "react";
import { useForm } from "react-hook-form";
import { z } from "zod/v4";
import { ProfileColor } from "../../constants/profile-colors";
import {
	type BrowserProfile,
	Platform,
	ProxyType,
	profileStore,
} from "../../stores/profile-store";
import { ColorPicker } from "../components/color-picker";
import { DEFAULT_FINGERPRINTS } from "../components/default-fingerprints";
import { SHEET_ANIMATION_MS } from "../constants";

const formSchema = z.object({
	name: z.string().min(1, "Profile name is required"),
	group: z.string().optional(),
	color: z.nativeEnum(ProfileColor),
	platform: z.nativeEnum(Platform),
	proxyEnabled: z.boolean(),
	proxy: z
		.object({
			proxyType: z.nativeEnum(ProxyType),
			host: z.string(),
			port: z.number().int().min(1).max(65535),
			username: z.string().optional(),
			password: z.string().optional(),
		})
		.optional(),
});

type FormValues = z.infer<typeof formSchema>;

function detectPlatform(): Platform {
	const userAgent = navigator.userAgent.toLowerCase();

	if (userAgent.includes("mac")) {
		return Platform.MACOS;
	}

	return userAgent.includes("linux") ? Platform.LINUX : Platform.WINDOWS;
}

function buildDefaults(profile?: BrowserProfile): FormValues {
	if (profile) {
		return {
			name: profile.name,
			group: profile.group ?? "",
			color: profile.color,
			platform: profile.fingerprint.platform,
			proxyEnabled: profile.proxy !== null,
			proxy: profile.proxy
				? {
						proxyType: profile.proxy.proxyType,
						host: profile.proxy.host,
						port: profile.proxy.port,
						username: profile.proxy.username ?? "",
						password: profile.proxy.password ?? "",
					}
				: {
						proxyType: ProxyType.HTTP,
						host: "",
						port: 8080,
						username: "",
						password: "",
					},
		};
	}

	return {
		name: "",
		group: "",
		color: ProfileColor.BLUE,
		platform: detectPlatform(),
		proxyEnabled: false,
		proxy: {
			proxyType: ProxyType.HTTP,
			host: "",
			port: 8080,
			username: "",
			password: "",
		},
	};
}

function buildCreateInput(data: FormValues) {
	return {
		name: data.name,
		color: data.color,
		group: data.group || null,
		fingerprint: DEFAULT_FINGERPRINTS[data.platform],
		proxy:
			data.proxyEnabled && data.proxy?.host
				? {
						proxyType: data.proxy.proxyType,
						host: data.proxy.host,
						port: data.proxy.port,
						username: data.proxy.username || null,
						password: data.proxy.password || null,
					}
				: null,
	};
}

interface Props {
	onClose: () => void;
	profileId?: string;
}

export function ProfileSheet({ onClose, profileId }: Props) {
	const [open, setOpen] = useState(false);

	const profile = profileId
		? profileStore.getState().profiles.find((p) => p.id === profileId)
		: undefined;

	const isEditing = Boolean(profileId && profile);

	useEffect(() => {
		requestAnimationFrame(() => setOpen(true));
	}, []);

	const close = () => {
		setOpen(false);
		setTimeout(onClose, SHEET_ANIMATION_MS);
	};

	const form = useForm<FormValues>({
		resolver: zodResolver(formSchema),
		defaultValues: buildDefaults(profile),
	});

	const proxyEnabled = form.watch("proxyEnabled");

	const onSubmit = (data: FormValues) => {
		if (isEditing && profileId) {
			profileStore.getState().update(profileId, buildCreateInput(data));
		} else {
			profileStore.getState().create(buildCreateInput(data));
		}

		form.reset();
		close();
	};

	return (
		<Sheet open={open} onOpenChange={(open) => !open && close()}>
			<SheetContent>
				<SheetHeader>
					<SheetTitle>
						{isEditing ? "Edit profile" : "New profile"}
					</SheetTitle>
					<button
						type="button"
						onClick={close}
						className="flex h-6 w-6 items-center justify-center rounded text-[#71717a] transition-colors hover:text-foreground"
					>
						<X className="h-4 w-4" />
					</button>
				</SheetHeader>
				<Form {...form}>
					<form
						onSubmit={form.handleSubmit(onSubmit)}
						className="flex flex-1 flex-col"
					>
						<SheetBody className="space-y-4">
							<FormField
								control={form.control}
								name="name"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">Name</FormLabel>
										<FormControl>
											<Input
												placeholder="e.g. Work account"
												autoFocus
												{...field}
											/>
										</FormControl>
										<FormMessage />
									</FormItem>
								)}
							/>
							<FormField
								control={form.control}
								name="group"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">
											Group <span className="text-[#71717a]">(optional)</span>
										</FormLabel>
										<FormControl>
											<Input placeholder="Enter group name" {...field} />
										</FormControl>
									</FormItem>
								)}
							/>
							<FormField
								control={form.control}
								name="color"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">Color</FormLabel>
										<FormControl>
											<ColorPicker
												value={field.value}
												onChange={field.onChange}
											/>
										</FormControl>
									</FormItem>
								)}
							/>
							<FormField
								control={form.control}
								name="platform"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">Platform</FormLabel>
										<div className="flex gap-1">
											{(
												[
													Platform.WINDOWS,
													Platform.MACOS,
													Platform.LINUX,
												] as const
											).map((platform) => (
												<Button
													key={platform}
													type="button"
													variant={
														field.value === platform ? "default" : "outline"
													}
													className="h-8 flex-1 text-[11px] capitalize"
													onClick={() => field.onChange(platform)}
												>
													{platform.toLowerCase()}
												</Button>
											))}
										</div>
									</FormItem>
								)}
							/>
							<Separator />
							<FormField
								control={form.control}
								name="proxyEnabled"
								render={({ field }) => (
									<FormItem className="flex flex-row items-center justify-between">
										<FormLabel className="text-[11px]">Enable proxy</FormLabel>
										<FormControl>
											<Switch
												checked={field.value}
												onCheckedChange={field.onChange}
											/>
										</FormControl>
									</FormItem>
								)}
							/>
							{proxyEnabled ? (
								<div className="space-y-3 rounded-md border border-border p-3">
									<div className="flex gap-2">
										<FormField
											control={form.control}
											name="proxy.proxyType"
											render={({ field }) => (
												<FormItem>
													<Select
														onValueChange={field.onChange}
														defaultValue={field.value}
													>
														<FormControl>
															<SelectTrigger className="w-[100px]">
																<SelectValue />
															</SelectTrigger>
														</FormControl>
														<SelectContent>
															<SelectItem value={ProxyType.HTTP}>
																HTTP
															</SelectItem>
															<SelectItem value={ProxyType.HTTPS}>
																HTTPS
															</SelectItem>
															<SelectItem value={ProxyType.SOCKS4}>
																SOCKS4
															</SelectItem>
															<SelectItem value={ProxyType.SOCKS5}>
																SOCKS5
															</SelectItem>
														</SelectContent>
													</Select>
												</FormItem>
											)}
										/>
										<FormField
											control={form.control}
											name="proxy.host"
											render={({ field }) => (
												<FormItem className="flex-1">
													<FormControl>
														<Input placeholder="Host" {...field} />
													</FormControl>
													<FormMessage />
												</FormItem>
											)}
										/>
										<FormField
											control={form.control}
											name="proxy.port"
											render={({ field }) => (
												<FormItem className="w-20">
													<FormControl>
														<Input
															type="number"
															placeholder="Port"
															{...field}
															onChange={(e) =>
																field.onChange(Number(e.target.value))
															}
														/>
													</FormControl>
												</FormItem>
											)}
										/>
									</div>
									<div className="flex gap-2">
										<FormField
											control={form.control}
											name="proxy.username"
											render={({ field }) => (
												<FormItem className="flex-1">
													<FormControl>
														<Input
															placeholder="Username (optional)"
															{...field}
														/>
													</FormControl>
												</FormItem>
											)}
										/>
										<FormField
											control={form.control}
											name="proxy.password"
											render={({ field }) => (
												<FormItem className="flex-1">
													<FormControl>
														<Input
															type="password"
															placeholder="Password (optional)"
															{...field}
														/>
													</FormControl>
												</FormItem>
											)}
										/>
									</div>
								</div>
							) : null}
						</SheetBody>
						<SheetFooter>
							<Button
								type="button"
								variant="outline"
								onClick={() => {
									form.reset();
									close();
								}}
							>
								Cancel
							</Button>
							<Button type="submit">
								{isEditing ? "Save" : "Create"}
							</Button>
						</SheetFooter>
					</form>
				</Form>
			</SheetContent>
		</Sheet>
	);
}
```

- [ ] **Step 3: Verify typecheck passes**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/veil`

Expected: Will fail because `layout.tsx` still imports `CreateProfileSheet` from the old path. That's expected — we fix it in Task 6.

- [ ] **Step 4: Commit**

```bash
git add apps/veil/src/renderer/sheets/profile-sheet.tsx
git commit -m "feat: unify profile sheet to support both creation and editing"
```

---

### Task 6: Context Menu on Profile Items + Remove Trash Icon + Update References

**Files:**
- Modify: `apps/veil/src/renderer/app/layout.tsx`

- [ ] **Step 1: Update imports in `layout.tsx`**

In `apps/veil/src/renderer/app/layout.tsx`, replace the old imports:

Replace:
```typescript
import { Trash2, X } from "lucide-react";
```
With:
```typescript
import { Pencil, Trash2, X } from "lucide-react";
```

Replace:
```typescript
import { CreateProfileSheet } from "../sheets/create-profile";
```
With:
```typescript
import {
	ContextMenu,
	ContextMenuContent,
	ContextMenuItem,
	ContextMenuSeparator,
	ContextMenuTrigger,
} from "@pane/ui/components/context-menu";
import { ProfileSheet } from "../sheets/profile-sheet";
```

- [ ] **Step 2: Refactor `SidebarProfileItem` — add context menu, remove trash icon**

Replace the entire `SidebarProfileItem` function (lines 99–199) with:

```typescript
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
		<ContextMenu>
			<ContextMenuTrigger asChild>
				<ProfileItem ref={ref} style={{ opacity: isDragSource ? 0.4 : 1 }}>
					<ProfileHeader
						ref={handleRef}
						color={profile.color}
						active={isRunning}
						onClick={handleToggle}
					>
						<ProfileName>{profile.name}</ProfileName>

						{!expanded && isRunning ? (
							<ProfileBadge>{profile.tabs.length}</ProfileBadge>
						) : null}
					</ProfileHeader>

					{expanded ? (
						<DragDropProvider
							sensors={SENSORS}
							modifiers={[RestrictToVerticalAxis]}
							onDragEnd={(event) => {
								if (event.canceled) {
									return;
								}

								const { source } = event.operation;

								if (isSortable(source)) {
									if (source.initialIndex !== source.index) {
										profileStore
											.getState()
											.reorderTabs(profile.id, source.initialIndex, source.index);
									}
								}
							}}
						>
							<ProfileTabs>
								{profile.tabs.map((tab, index) => (
									<SortableTab key={tab.id} tab={tab} index={index} />
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
				</ProfileItem>
			</ContextMenuTrigger>

			<ContextMenuContent>
				<ContextMenuItem
					onClick={() => surface.open(ProfileSheet, { profileId: profile.id })}
				>
					<Pencil className="mr-2 h-3.5 w-3.5" />
					Edit profile
				</ContextMenuItem>

				<ContextMenuSeparator />

				<ContextMenuItem
					className="text-red-400 focus:text-red-400"
					onClick={() =>
						trpc.profiles.remove.mutate({ profileId: profile.id })
					}
				>
					<Trash2 className="mr-2 h-3.5 w-3.5" />
					Delete profile
				</ContextMenuItem>
			</ContextMenuContent>
		</ContextMenu>
	);
}
```

Key changes from the original:
- Wrapped in `ContextMenu` + `ContextMenuTrigger asChild`
- Removed the trash icon hover animation `<div>` and the inline `Trash2` from `ProfileHeader`'s children
- Added `ContextMenuContent` with "Edit profile" and "Delete profile" items

- [ ] **Step 3: Update the `SidebarNewButton` onClick in `Layout`**

In the same file, in the `Layout` component, replace:

```typescript
<SidebarNewButton onClick={() => surface.open(CreateProfileSheet)} />
```

With:

```typescript
<SidebarNewButton onClick={() => surface.open(ProfileSheet)} />
```

- [ ] **Step 4: Remove unused `Settings` import if only `Trash2` was removed**

Check: `Settings` is still used on line ~407 (`<Settings className="h-3.5 w-3.5" />`), so keep it. `Trash2` is still used in the context menu. `X` is still used in `SortableTab`. The only new import is `Pencil`. All good.

- [ ] **Step 5: Verify typecheck passes**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/veil`

Expected: Clean pass.

- [ ] **Step 6: Verify the full project builds**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck`

Expected: All packages pass typecheck.

- [ ] **Step 7: Commit**

```bash
git add apps/veil/src/renderer/app/layout.tsx
git commit -m "feat: add context menu on profiles with edit and delete actions"
```

---

### Task 7: Final Verification

- [ ] **Step 1: Run full typecheck across all packages**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck`

Expected: All packages pass with zero errors.

- [ ] **Step 2: Run lint**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck && bun run --filter=@pane/veil biome check apps/veil/src`

If biome is configured at root level instead, run: `bunx biome check apps/veil/src packages/ui/src`

Expected: Zero errors, zero warnings.

- [ ] **Step 3: Manual smoke test**

Start the dev server and verify:
1. Right-click a profile in the sidebar → context menu appears with "Edit profile" and "Delete profile"
2. Click "Edit profile" → sheet opens pre-populated with the profile's data, title says "Edit profile", button says "Save"
3. Change the name and save → profile updates in the sidebar
4. Click "+ New" → sheet opens empty, title says "New profile", button says "Create"
5. Create a new profile → appears in sidebar
6. Right-click → "Delete profile" → profile removed
7. The old hover trash icon is gone from profile headers
