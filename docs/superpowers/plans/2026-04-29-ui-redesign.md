# UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor Pane's UI to an Arc-style minimal dark theme with two-tone backgrounds, profile colors, composition pattern components, and right-side sheets replacing dialogs.

**Architecture:** The window base is a dark sidebar background. The content area floats as a raised panel with rounded corners and shadow. All app-level components (sidebar, address bar, profiles) are rebuilt as composition pattern components with named exports. Shared primitives (Sheet, Button, Input) come from `@pane/ui` via shadcn.

**Tech Stack:** Electron, React, Zustand, Tailwind CSS v4, shadcn/ui, Radix UI, react-hook-form, zod

---

## File Structure

### New files
- `packages/ui/src/components/sheet.tsx` — shadcn Sheet primitive (from shadcn CLI)
- `apps/desktop/src/renderer/components/sidebar/sidebar.tsx` — composition Sidebar components
- `apps/desktop/src/renderer/components/sidebar/profile-item.tsx` — ProfileItem, ProfileName, ProfileBadge, ProfileTabs
- `apps/desktop/src/renderer/components/sidebar/tab-item.tsx` — TabItem, TabFavicon, TabTitle, TabNew
- `apps/desktop/src/renderer/components/address-bar/address-bar.tsx` — composition AddressBar components
- `apps/desktop/src/renderer/components/content-panel.tsx` — raised panel wrapper
- `apps/desktop/src/renderer/components/empty-state.tsx` — empty browser state
- `apps/desktop/src/renderer/components/create-profile-sheet.tsx` — sheet form replacing dialog
- `apps/desktop/src/renderer/components/color-picker.tsx` — profile color picker

### Modified files
- `apps/desktop/src/renderer/styles/globals.css` — updated theme tokens
- `apps/desktop/src/renderer/components/app.tsx` — new layout with ContentPanel
- `apps/desktop/src/renderer/components/settings-page.tsx` — restyled
- `apps/desktop/src/stores/profile-store.ts` — add `color` field to BrowserProfile
- `apps/desktop/src/main/browser/tab-manager.ts` — update SIDEBAR_WIDTH and TOOLBAR_HEIGHT constants

### Deleted files
- `apps/desktop/src/renderer/components/sidebar.tsx` — replaced by sidebar/ directory
- `apps/desktop/src/renderer/components/address-bar.tsx` — replaced by address-bar/ directory
- `apps/desktop/src/renderer/components/create-profile-dialog.tsx` — replaced by create-profile-sheet.tsx

---

### Task 1: Update theme tokens in globals.css

**Files:**
- Modify: `apps/desktop/src/renderer/styles/globals.css`

- [ ] **Step 1: Update the `@theme` block with new color values**

```css
@theme {
  --color-background: #0a0a0c;
  --color-foreground: #fafafa;
  --color-card: #161618;
  --color-card-foreground: #fafafa;
  --color-popover: #161618;
  --color-popover-foreground: #fafafa;
  --color-primary: #fafafa;
  --color-primary-foreground: #18181b;
  --color-secondary: #1c1c1f;
  --color-secondary-foreground: #fafafa;
  --color-muted: #1c1c1f;
  --color-muted-foreground: #71717a;
  --color-accent: #1c1c1f;
  --color-accent-foreground: #e4e4e7;
  --color-destructive: #7f1d1d;
  --color-destructive-foreground: #fafafa;
  --color-border: rgba(255, 255, 255, 0.06);
  --color-input: rgba(255, 255, 255, 0.06);
  --color-ring: #52525b;
  --radius: 0.5rem;
}
```

- [ ] **Step 2: Verify the app still renders**

Run: `cd apps/desktop && bun run dev`

Check that the window opens with the new darker base colors. The existing components will look slightly different but shouldn't break.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/styles/globals.css
git commit -m "style: update theme tokens for minimal dark redesign"
```

---

### Task 2: Add `color` field to BrowserProfile

**Files:**
- Modify: `apps/desktop/src/stores/profile-store.ts`

- [ ] **Step 1: Add ProfileColor enum and color field**

Add above the `BrowserProfile` interface:

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

- [ ] **Step 2: Add `color` to BrowserProfile interface**

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
  isExpanded: boolean;
  createdAt: string;
  updatedAt: string;
}
```

- [ ] **Step 3: Update CreateInput to include color, update create action default**

The `CreateInput` type already derives from `BrowserProfile` via `Omit`, so `color` will be included automatically. No change needed to the type.

- [ ] **Step 4: Update the persist `merge` function to handle existing profiles without color**

In the `merge` callback, add a default color for profiles that were persisted before this field existed:

```ts
merge: (persisted, current) => ({
  ...current,
  ...(persisted as Partial<ProfileState>),
  profiles: ((persisted as Partial<ProfileState>)?.profiles ?? []).map(
    (p) => ({
      ...p,
      color: p.color ?? ProfileColor.BLUE,
      tabs: p.tabs.map((t) => ({ ...t, favicon: t.favicon ?? "" })),
      isExpanded: p.tabs.length > 0,
    }),
  ),
}),
```

- [ ] **Step 5: Run typecheck**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop 2>&1 | tail -5`

Expected: Existing callers of `create()` will fail because they don't pass `color`. That's expected — we'll fix them in Task 8 (create-profile-sheet).

- [ ] **Step 6: Commit**

```bash
git add apps/desktop/src/stores/profile-store.ts
git commit -m "feat: add ProfileColor enum and color field to BrowserProfile"
```

---

### Task 3: Install shadcn Sheet component

**Files:**
- Create: `packages/ui/src/components/sheet.tsx`

- [ ] **Step 1: Check if @radix-ui/react-dialog is already installed**

Run: `grep "react-dialog" packages/ui/package.json`

Expected: Already present (used by dialog.tsx).

- [ ] **Step 2: Create the sheet component**

The shadcn Sheet is built on `@radix-ui/react-dialog` (same as Dialog). Create `packages/ui/src/components/sheet.tsx` with the shadcn sheet implementation. The sheet uses `DialogPrimitive` from Radix under the hood but with slide-in positioning instead of centered.

```tsx
import * as SheetPrimitive from "@radix-ui/react-dialog";
import { X } from "lucide-react";
import type { ComponentPropsWithoutRef, ElementRef } from "react";
import { forwardRef } from "react";

import { cn } from "../cn";

const Sheet = SheetPrimitive.Root;
const SheetTrigger = SheetPrimitive.Trigger;
const SheetClose = SheetPrimitive.Close;
const SheetPortal = SheetPrimitive.Portal;

const SheetOverlay = forwardRef<
  ElementRef<typeof SheetPrimitive.Overlay>,
  ComponentPropsWithoutRef<typeof SheetPrimitive.Overlay>
>(({ className, ...props }, ref) => (
  <SheetPrimitive.Overlay
    ref={ref}
    className={cn(
      "data-[state=closed]:fade-out-0 data-[state=open]:fade-in-0 fixed inset-0 z-50 bg-black/40 data-[state=closed]:animate-out data-[state=open]:animate-in",
      className,
    )}
    {...props}
  />
));

SheetOverlay.displayName = SheetPrimitive.Overlay.displayName;

const SheetContent = forwardRef<
  ElementRef<typeof SheetPrimitive.Content>,
  ComponentPropsWithoutRef<typeof SheetPrimitive.Content>
>(({ className, children, ...props }, ref) => (
  <SheetPortal>
    <SheetOverlay />
    <SheetPrimitive.Content
      ref={ref}
      className={cn(
        "fixed top-2 right-2 bottom-2 z-50 flex w-[360px] flex-col rounded-[10px] bg-card shadow-[0_0_0_1px_rgba(255,255,255,0.08),0_8px_40px_rgba(0,0,0,0.6)] duration-200 data-[state=closed]:animate-out data-[state=open]:animate-in data-[state=closed]:fade-out-0 data-[state=open]:fade-in-0 data-[state=closed]:slide-out-to-right data-[state=open]:slide-in-from-right",
        className,
      )}
      {...props}
    >
      {children}
    </SheetPrimitive.Content>
  </SheetPortal>
));

SheetContent.displayName = SheetPrimitive.Content.displayName;

const SheetHeader = ({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) => (
  <div
    className={cn(
      "flex items-center justify-between border-b border-[rgba(255,255,255,0.05)] px-5 py-4",
      className,
    )}
    {...props}
  />
);

SheetHeader.displayName = "SheetHeader";

const SheetTitle = forwardRef<
  ElementRef<typeof SheetPrimitive.Title>,
  ComponentPropsWithoutRef<typeof SheetPrimitive.Title>
>(({ className, ...props }, ref) => (
  <SheetPrimitive.Title
    ref={ref}
    className={cn("text-sm font-medium text-accent-foreground", className)}
    {...props}
  />
));

SheetTitle.displayName = SheetPrimitive.Title.displayName;

const SheetDescription = forwardRef<
  ElementRef<typeof SheetPrimitive.Description>,
  ComponentPropsWithoutRef<typeof SheetPrimitive.Description>
>(({ className, ...props }, ref) => (
  <SheetPrimitive.Description
    ref={ref}
    className={cn("text-sm text-muted-foreground", className)}
    {...props}
  />
));

SheetDescription.displayName = SheetPrimitive.Description.displayName;

const SheetBody = ({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) => (
  <div className={cn("flex-1 overflow-auto px-5 py-5", className)} {...props} />
);

SheetBody.displayName = "SheetBody";

const SheetFooter = ({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) => (
  <div
    className={cn(
      "flex justify-end gap-2 border-t border-[rgba(255,255,255,0.05)] px-5 py-4",
      className,
    )}
    {...props}
  />
);

SheetFooter.displayName = "SheetFooter";

export {
  Sheet,
  SheetBody,
  SheetClose,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetOverlay,
  SheetPortal,
  SheetTitle,
  SheetTrigger,
};
```

- [ ] **Step 3: Verify it builds**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/ui 2>&1 | tail -5`

Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add packages/ui/src/components/sheet.tsx
git commit -m "feat: add Sheet component to @pane/ui"
```

---

### Task 4: Create ContentPanel wrapper component

**Files:**
- Create: `apps/desktop/src/renderer/components/content-panel.tsx`

- [ ] **Step 1: Create the ContentPanel component**

This is the raised card that holds the address bar and web content / settings. It's a simple styled wrapper.

```tsx
import type { HTMLAttributes } from "react";

import { cn } from "@pane/ui/cn";

export function ContentPanel({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        "m-2 ml-0 flex flex-1 flex-col overflow-hidden rounded-[10px] bg-card shadow-[0_0_0_1px_rgba(255,255,255,0.06),0_4px_24px_rgba(0,0,0,0.4)]",
        className,
      )}
      {...props}
    />
  );
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/renderer/components/content-panel.tsx
git commit -m "feat: add ContentPanel wrapper component"
```

---

### Task 5: Create EmptyState component

**Files:**
- Create: `apps/desktop/src/renderer/components/empty-state.tsx`

- [ ] **Step 1: Create the empty state component**

```tsx
export function EmptyState() {
  return (
    <div className="flex flex-1 items-center justify-center">
      <div className="text-center">
        <span className="text-lg font-medium text-[#27272a]">Pane</span>
        <p className="mt-1 text-xs text-[#3f3f46]">
          Open a profile to start browsing
        </p>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/renderer/components/empty-state.tsx
git commit -m "feat: add EmptyState component"
```

---

### Task 6: Build composition Sidebar components

**Files:**
- Create: `apps/desktop/src/renderer/components/sidebar/sidebar.tsx`
- Create: `apps/desktop/src/renderer/components/sidebar/profile-item.tsx`
- Create: `apps/desktop/src/renderer/components/sidebar/tab-item.tsx`

- [ ] **Step 1: Create sidebar directory**

Run: `mkdir -p /Users/andrevictor/www/pane/apps/desktop/src/renderer/components/sidebar`

- [ ] **Step 2: Create sidebar.tsx — shell components**

```tsx
import type { HTMLAttributes } from "react";

import { cn } from "@pane/ui/cn";

export function Sidebar({
  className,
  ...props
}: HTMLAttributes<HTMLElement>) {
  return (
    <aside
      className={cn("flex w-[220px] shrink-0 flex-col", className)}
      {...props}
    />
  );
}

export function SidebarHeader({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn("flex h-11 items-center px-3.5 gap-2", className)}
      style={{ WebkitAppRegion: "drag" } as React.CSSProperties}
      {...props}
    />
  );
}

export function SidebarTitle({
  className,
  ...props
}: HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn("text-[11px] font-semibold text-[#52525b] ml-1", className)}
      {...props}
    />
  );
}

export function SidebarContent({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn("flex-1 overflow-y-auto px-2.5 py-1", className)}
      {...props}
    />
  );
}

export function SidebarFooter({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div className={cn("px-2.5 pb-2", className)} {...props} />
  );
}

export function SidebarSeparator({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn("my-1.5 h-px bg-[rgba(255,255,255,0.04)]", className)}
      {...props}
    />
  );
}

export function SidebarNewButton({
  className,
  ...props
}: HTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      type="button"
      className={cn(
        "flex w-full items-center gap-1.5 rounded-md px-2 py-1.5 text-xs text-[#3f3f46] transition-colors hover:bg-accent",
        className,
      )}
      {...props}
    >
      <span className="text-sm font-light">+</span>
      <span>New</span>
    </button>
  );
}

export function SidebarSettingsButton({
  className,
  active,
  ...props
}: HTMLAttributes<HTMLButtonElement> & { active?: boolean }) {
  return (
    <button
      type="button"
      className={cn(
        "flex w-full items-center gap-1.5 rounded-md px-2 py-1.5 text-xs transition-colors",
        active
          ? "bg-[rgba(255,255,255,0.05)] text-accent-foreground"
          : "text-[#52525b] hover:bg-accent hover:text-accent-foreground",
        className,
      )}
      {...props}
    />
  );
}
```

- [ ] **Step 3: Create profile-item.tsx — profile composition components**

```tsx
import type { HTMLAttributes } from "react";

import { cn } from "@pane/ui/cn";
import { PROFILE_COLOR_HEX, type ProfileColor } from "../../../stores/profile-store";

interface ProfileItemProps extends HTMLAttributes<HTMLDivElement> {
  color: ProfileColor;
  active?: boolean;
}

export function ProfileItem({
  className,
  color,
  active,
  ...props
}: ProfileItemProps) {
  const hex = PROFILE_COLOR_HEX[color];

  return (
    <div className={cn("mb-1.5", className)} {...props} />
  );
}

interface ProfileHeaderProps extends HTMLAttributes<HTMLButtonElement> {
  color: ProfileColor;
  active?: boolean;
  expanded?: boolean;
}

export function ProfileHeader({
  className,
  color,
  active,
  expanded,
  children,
  ...props
}: ProfileHeaderProps) {
  const hex = PROFILE_COLOR_HEX[color];

  return (
    <button
      type="button"
      className={cn(
        "group flex w-full items-center gap-1.5 rounded-md px-2 py-1.5 text-xs transition-colors",
        active
          ? "text-[#d4d4d8]"
          : "text-[#71717a] hover:bg-accent",
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
      <span className="text-[10px] text-[#52525b]">
        {expanded ? "▼" : "▶"}
      </span>
      <div
        className="h-3.5 w-1 shrink-0 rounded-sm"
        style={{ background: hex }}
      />
      {children}
    </button>
  );
}

export function ProfileName({
  className,
  ...props
}: HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn("flex-1 truncate text-left text-xs font-medium", className)}
      {...props}
    />
  );
}

export function ProfileBadge({
  className,
  ...props
}: HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn("text-[10px] text-[#52525b]", className)}
      {...props}
    />
  );
}

export function ProfileTabs({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div className={cn("ml-2.5 mt-1.5", className)} {...props} />
  );
}
```

- [ ] **Step 4: Create tab-item.tsx — tab composition components**

```tsx
import type { HTMLAttributes, ImgHTMLAttributes } from "react";

import { cn } from "@pane/ui/cn";
import { Globe } from "lucide-react";

interface TabItemProps extends HTMLAttributes<HTMLButtonElement> {
  active?: boolean;
}

export function TabItem({
  className,
  active,
  ...props
}: TabItemProps) {
  return (
    <button
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

export function TabFavicon({
  src,
  className,
  ...props
}: ImgHTMLAttributes<HTMLImageElement>) {
  if (!src) {
    return <Globe className={cn("h-3 w-3 shrink-0", className)} />;
  }

  return (
    <img
      src={src}
      alt=""
      className={cn("h-3 w-3 shrink-0 rounded-sm", className)}
      {...props}
    />
  );
}

export function TabTitle({
  className,
  ...props
}: HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn("flex-1 truncate text-left", className)}
      {...props}
    />
  );
}

export function TabNew({
  className,
  ...props
}: HTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      type="button"
      className={cn(
        "ml-1 flex w-full items-center gap-1.5 rounded-[5px] px-2 py-1 text-[11px] text-[#3f3f46] transition-colors hover:bg-accent hover:text-accent-foreground",
        className,
      )}
      {...props}
    >
      <span className="text-[10px]">+</span> New tab
    </button>
  );
}
```

- [ ] **Step 5: Verify typecheck**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop 2>&1 | tail -10`

- [ ] **Step 6: Commit**

```bash
git add apps/desktop/src/renderer/components/sidebar/
git commit -m "feat: add composition sidebar components"
```

---

### Task 7: Build composition AddressBar components

**Files:**
- Create: `apps/desktop/src/renderer/components/address-bar/address-bar.tsx`

- [ ] **Step 1: Create address-bar directory**

Run: `mkdir -p /Users/andrevictor/www/pane/apps/desktop/src/renderer/components/address-bar`

- [ ] **Step 2: Create address-bar.tsx**

```tsx
import type { HTMLAttributes, InputHTMLAttributes } from "react";
import { forwardRef } from "react";

import { cn } from "@pane/ui/cn";
import { ArrowLeft, ArrowRight, RotateCw } from "lucide-react";
import { PROFILE_COLOR_HEX, type ProfileColor } from "../../../stores/profile-store";

export function AddressBar({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        "flex items-center gap-1.5 border-b border-[rgba(255,255,255,0.05)] px-2.5 py-1.5",
        className,
      )}
      {...props}
    />
  );
}

export function AddressBarNav({
  onBack,
  onForward,
  onReload,
}: {
  onBack: () => void;
  onForward: () => void;
  onReload: () => void;
}) {
  const btnClass =
    "flex h-[26px] w-[26px] items-center justify-center rounded text-[#52525b] transition-colors hover:bg-accent hover:text-accent-foreground";

  return (
    <div className="flex gap-0.5">
      <button type="button" className={btnClass} onClick={onBack}>
        <ArrowLeft className="h-3.5 w-3.5" />
      </button>
      <button type="button" className={btnClass} onClick={onForward}>
        <ArrowRight className="h-3.5 w-3.5" />
      </button>
      <button type="button" className={btnClass} onClick={onReload}>
        <RotateCw className="h-3 w-3" />
      </button>
    </div>
  );
}

export const AddressBarInput = forwardRef<
  HTMLInputElement,
  InputHTMLAttributes<HTMLInputElement>
>(({ className, ...props }, ref) => (
  <input
    ref={ref}
    type="text"
    className={cn(
      "h-[30px] flex-1 rounded-[5px] border border-input bg-[rgba(255,255,255,0.03)] px-2.5 text-xs text-foreground placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-ring",
      className,
    )}
    {...props}
  />
));

AddressBarInput.displayName = "AddressBarInput";

interface AddressBarProfileBadgeProps extends HTMLAttributes<HTMLDivElement> {
  color: ProfileColor;
}

export function AddressBarProfileBadge({
  className,
  color,
  children,
  ...props
}: AddressBarProfileBadgeProps) {
  const hex = PROFILE_COLOR_HEX[color];

  return (
    <div
      className={cn(
        "flex h-[30px] items-center rounded-[5px] px-2.5 text-[11px] font-medium",
        className,
      )}
      style={{
        background: `color-mix(in srgb, ${hex} 8%, transparent)`,
        border: `1px solid color-mix(in srgb, ${hex} 30%, transparent)`,
        color: hex,
      }}
      {...props}
    >
      {children}
    </div>
  );
}

export function AddressBarExtensions({
  className,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return <div className={cn("flex gap-0.5", className)} {...props} />;
}
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/components/address-bar/
git commit -m "feat: add composition AddressBar components"
```

---

### Task 8: Create color picker and create-profile-sheet

**Files:**
- Create: `apps/desktop/src/renderer/components/color-picker.tsx`
- Create: `apps/desktop/src/renderer/components/create-profile-sheet.tsx`

- [ ] **Step 1: Create color-picker.tsx**

```tsx
import { cn } from "@pane/ui/cn";
import { PROFILE_COLOR_HEX, ProfileColor } from "../../stores/profile-store";

interface ColorPickerProps {
  value: ProfileColor;
  onChange: (color: ProfileColor) => void;
}

const COLORS = Object.values(ProfileColor);

export function ColorPicker({ value, onChange }: ColorPickerProps) {
  return (
    <div className="flex flex-wrap gap-1.5">
      {COLORS.map((color) => {
        const hex = PROFILE_COLOR_HEX[color];
        const selected = value === color;

        return (
          <button
            key={color}
            type="button"
            onClick={() => onChange(color)}
            className={cn(
              "h-6 w-6 rounded-md transition-shadow",
              selected && "ring-2 ring-offset-2 ring-offset-background",
            )}
            style={{
              background: hex,
              ...(selected ? { boxShadow: `0 0 0 2px var(--color-background), 0 0 0 3px ${hex}` } : {}),
            }}
          />
        );
      })}
    </div>
  );
}
```

- [ ] **Step 2: Create create-profile-sheet.tsx**

This replaces `create-profile-dialog.tsx`. Same form logic, new container + color field.

```tsx
import { zodResolver } from "@hookform/resolvers/zod";
import { Button } from "@pane/ui/components/button";
import { Input } from "@pane/ui/components/input";
import {
  Form,
  FormControl,
  FormField,
  FormItem,
  FormLabel,
  FormMessage,
} from "@pane/ui/components/form";
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
import { useForm } from "react-hook-form";
import { toast } from "sonner";
import { z } from "zod/v4";

import {
  type Fingerprint,
  ProfileColor,
  profileStore,
} from "../../stores/profile-store";
import { ColorPicker } from "./color-picker";

const FINGERPRINTS: Record<string, Fingerprint> = {
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

const formSchema = z.object({
  name: z.string().min(1, "Profile name is required"),
  group: z.string().optional(),
  color: z.nativeEnum(ProfileColor),
  platform: z.enum(["windows", "macos", "linux"]),
  proxyEnabled: z.boolean(),
  proxy: z
    .object({
      proxyType: z.enum(["http", "https", "socks4", "socks5"]),
      host: z.string(),
      port: z.coerce.number().int().min(1).max(65535),
      username: z.string().optional(),
      password: z.string().optional(),
    })
    .optional(),
});

type FormValues = z.infer<typeof formSchema>;

function detectPlatform(): "windows" | "macos" | "linux" {
  const ua = navigator.userAgent.toLowerCase();

  if (ua.includes("mac")) {
    return "macos";
  }

  return ua.includes("linux") ? "linux" : "windows";
}

interface Props {
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function CreateProfileSheet({ open, onOpenChange }: Props) {
  const form = useForm<FormValues>({
    resolver: zodResolver(formSchema),
    defaultValues: {
      name: "",
      group: "",
      color: ProfileColor.BLUE,
      platform: detectPlatform(),
      proxyEnabled: false,
      proxy: {
        proxyType: "http",
        host: "",
        port: 8080,
        username: "",
        password: "",
      },
    },
  });

  const proxyEnabled = form.watch("proxyEnabled");

  const onSubmit = (data: FormValues) => {
    profileStore.getState().create({
      name: data.name,
      color: data.color,
      group: data.group || null,
      notes: null,
      fingerprint: FINGERPRINTS[data.platform],
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

    toast.success(`Profile "${data.name}" created`);
    form.reset();
    onOpenChange(false);
  };

  return (
    <Sheet open={open} onOpenChange={onOpenChange}>
      <SheetContent>
        <SheetHeader>
          <SheetTitle>New profile</SheetTitle>
          <button
            type="button"
            onClick={() => onOpenChange(false)}
            className="flex h-6 w-6 items-center justify-center rounded text-[#52525b] transition-colors hover:text-foreground"
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
                      Group <span className="text-[#3f3f46]">(optional)</span>
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
                      {(["windows", "macos", "linux"] as const).map((p) => (
                        <Button
                          key={p}
                          type="button"
                          variant={field.value === p ? "default" : "outline"}
                          className="flex-1 text-[11px] h-8 capitalize"
                          onClick={() => field.onChange(p)}
                        >
                          {p}
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
                              <SelectItem value="http">HTTP</SelectItem>
                              <SelectItem value="https">HTTPS</SelectItem>
                              <SelectItem value="socks4">SOCKS4</SelectItem>
                              <SelectItem value="socks5">SOCKS5</SelectItem>
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
                  onOpenChange(false);
                }}
              >
                Cancel
              </Button>
              <Button type="submit">Create</Button>
            </SheetFooter>
          </form>
        </Form>
      </SheetContent>
    </Sheet>
  );
}
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/components/color-picker.tsx apps/desktop/src/renderer/components/create-profile-sheet.tsx
git commit -m "feat: add ColorPicker and CreateProfileSheet components"
```

---

### Task 9: Rewrite App layout with new components

**Files:**
- Modify: `apps/desktop/src/renderer/components/app.tsx`

- [ ] **Step 1: Rewrite app.tsx to use ContentPanel and new layout**

```tsx
import { Toaster } from "sonner";
import { useStore } from "zustand/react";

import { navigationStore, Page } from "../../stores/navigation-store";
import { tabStore } from "../../stores/tab-store";
import { ContentPanel } from "./content-panel";
import { EmptyState } from "./empty-state";
import { SettingsPage } from "./settings-page";
import { BrowserAddressBar } from "./address-bar/address-bar-connected";
import { SidebarConnected } from "./sidebar/sidebar-connected";

export function App() {
  const page = useStore(navigationStore, (s) => s.page);
  const activeTabId = useStore(tabStore, (s) => s.activeTabId);

  return (
    <div className="flex h-screen bg-background text-foreground">
      <SidebarConnected />

      <ContentPanel>
        {page === Page.BROWSER ? (
          <>
            {activeTabId ? <BrowserAddressBar /> : null}
            {!activeTabId ? <EmptyState /> : null}
          </>
        ) : null}

        {page === Page.SETTINGS ? <SettingsPage /> : null}
      </ContentPanel>

      <Toaster theme="dark" />
    </div>
  );
}
```

Note: This references `SidebarConnected` and `BrowserAddressBar` — the "connected" versions that wire up store state to the composition components. These are created in the next task.

- [ ] **Step 2: Commit (will not typecheck yet — depends on Task 10)**

```bash
git add apps/desktop/src/renderer/components/app.tsx
git commit -m "refactor: update App layout with ContentPanel and composition components"
```

---

### Task 10: Create connected Sidebar and AddressBar

**Files:**
- Create: `apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx`
- Create: `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`

- [ ] **Step 1: Create sidebar-connected.tsx**

This file wires store state to the composition components from sidebar.tsx, profile-item.tsx, and tab-item.tsx.

```tsx
import { Settings, Trash2, X } from "lucide-react";
import { useState } from "react";
import { toast } from "sonner";
import { useStore } from "zustand/react";

import { navigationStore, Page } from "../../../stores/navigation-store";
import { profileStore } from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import { CreateProfileSheet } from "../create-profile-sheet";
import { ProfileHeader, ProfileItem, ProfileName, ProfileBadge, ProfileTabs } from "./profile-item";
import { Sidebar, SidebarContent, SidebarFooter, SidebarHeader, SidebarNewButton, SidebarSeparator, SidebarSettingsButton, SidebarTitle } from "./sidebar";
import { TabFavicon, TabItem, TabNew, TabTitle } from "./tab-item";

export function SidebarConnected() {
  const profiles = useStore(profileStore, (s) => s.profiles);
  const activeTabId = useStore(tabStore, (s) => s.activeTabId);
  const page = useStore(navigationStore, (s) => s.page);
  const [sheetOpen, setSheetOpen] = useState(false);

  const handleSheetChange = (open: boolean) => {
    setSheetOpen(open);

    if (open) {
      window.pane.tabs.hideAll();
    } else if (navigationStore.getState().page === Page.BROWSER) {
      window.pane.tabs.showActive();
    }
  };

  return (
    <Sidebar>
      <SidebarHeader>
        <SidebarTitle>Pane</SidebarTitle>
      </SidebarHeader>

      <SidebarContent>
        {profiles.map((profile) => {
          const isRunning = profile.tabs.length > 0;

          return (
            <ProfileItem key={profile.id} color={profile.color} active={isRunning}>
              <ProfileHeader
                color={profile.color}
                active={isRunning}
                expanded={profile.isExpanded}
                onClick={() => profileStore.getState().toggleExpanded(profile.id)}
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
        })}
      </SidebarContent>

      <SidebarFooter>
        <SidebarNewButton onClick={() => handleSheetChange(true)} />
        <SidebarSeparator />
        <SidebarSettingsButton
          active={page === Page.SETTINGS}
          onClick={() => navigationStore.getState().navigate(Page.SETTINGS)}
        >
          <Settings className="h-3.5 w-3.5" />
          Settings
        </SidebarSettingsButton>
      </SidebarFooter>

      <CreateProfileSheet open={sheetOpen} onOpenChange={handleSheetChange} />
    </Sidebar>
  );
}
```

- [ ] **Step 2: Create address-bar-connected.tsx**

```tsx
import { useState } from "react";
import { useStore } from "zustand/react";

import { profileStore, type ProfileColor, ProfileColor as PC } from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import {
  AddressBar,
  AddressBarExtensions,
  AddressBarInput,
  AddressBarNav,
  AddressBarProfileBadge,
} from "./address-bar";

export function BrowserAddressBar() {
  const activeTabId = useStore(tabStore, (s) => s.activeTabId);
  const profiles = useStore(profileStore, (s) => s.profiles);

  let activeUrl = "";
  let profileName = "";
  let profileColor: ProfileColor = PC.BLUE;

  for (const profile of profiles) {
    const tab = profile.tabs.find((t) => t.id === activeTabId);

    if (tab) {
      activeUrl = tab.url;
      profileName = profile.name;
      profileColor = profile.color;
      break;
    }
  }

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

      <form onSubmit={handleSubmit} className="flex-1">
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

      {profileName ? (
        <AddressBarProfileBadge color={profileColor}>
          {profileName}
        </AddressBarProfileBadge>
      ) : null}

      <AddressBarExtensions />
    </AddressBar>
  );
}
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx
git commit -m "feat: add connected sidebar and address bar components"
```

---

### Task 11: Restyle settings page

**Files:**
- Modify: `apps/desktop/src/renderer/components/settings-page.tsx`

- [ ] **Step 1: Update settings page styling**

```tsx
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
import { useEffect } from "react";
import { useForm } from "react-hook-form";
import { toast } from "sonner";
import { z } from "zod/v4";
import { useStore } from "zustand/react";

import { settingsStore } from "../../stores/settings-store";

const settingsSchema = z.object({
  chromiumPath: z.string().min(1, "Browser path is required"),
});

type SettingsValues = z.infer<typeof settingsSchema>;

export function SettingsPage() {
  const settings = useStore(settingsStore, (s) => s.settings);

  const form = useForm<SettingsValues>({
    resolver: zodResolver(settingsSchema),
    defaultValues: { chromiumPath: settings.chromiumPath },
  });

  useEffect(() => {
    form.reset({ chromiumPath: settings.chromiumPath });
  }, [settings.chromiumPath, form]);

  const onSubmit = (data: SettingsValues) => {
    settingsStore.getState().save({ chromiumPath: data.chromiumPath });
    toast.success("Settings saved");
  };

  return (
    <div className="flex-1 overflow-auto px-10 py-8">
      <div className="max-w-[480px] space-y-6">
        <div>
          <h1 className="text-sm font-medium text-accent-foreground">
            Settings
          </h1>
          <p className="text-xs text-[#52525b]">
            Manage your application preferences.
          </p>
        </div>

        <div className="h-px bg-[rgba(255,255,255,0.05)]" />

        <div>
          <span className="text-[10px] font-medium uppercase tracking-wide text-muted-foreground">
            Browser
          </span>

          <Form {...form}>
            <form
              onSubmit={form.handleSubmit(onSubmit)}
              className="mt-3 space-y-4"
            >
              <FormField
                control={form.control}
                name="chromiumPath"
                render={({ field }) => (
                  <FormItem>
                    <FormLabel className="text-[11px]">
                      Executable path
                    </FormLabel>
                    <div className="flex gap-2">
                      <FormControl>
                        <Input
                          placeholder="/Applications/Google Chrome.app"
                          {...field}
                        />
                      </FormControl>
                      <Button
                        type="button"
                        variant="outline"
                        className="shrink-0"
                        onClick={() => window.pane.settings.detectBrowser()}
                      >
                        Auto-detect
                      </Button>
                    </div>
                    <FormMessage />
                  </FormItem>
                )}
              />
              <Button type="submit">Save</Button>
            </form>
          </Form>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/renderer/components/settings-page.tsx
git commit -m "style: restyle settings page to match redesign spec"
```

---

### Task 12: Update tab-manager bounds for new layout

**Files:**
- Modify: `apps/desktop/src/main/browser/tab-manager.ts`

- [ ] **Step 1: Update constants for new sidebar width and toolbar height**

The sidebar is now 220px. The content panel has 8px margin on top/right/bottom and 0 on left. The address bar outer padding is 6px top + 30px input + 6px bottom + 1px border = 43px. Plus the panel's 8px top margin.

```ts
const SIDEBAR_WIDTH = 220;
const TOOLBAR_HEIGHT = 51; // 8px panel margin-top + 43px address bar
const PANEL_MARGIN_RIGHT = 8;
const PANEL_MARGIN_BOTTOM = 8;
const PANEL_BORDER_RADIUS_OFFSET = 2;
```

Update `getContentBounds()`:

```ts
private getContentBounds(): Electron.Rectangle {
  const [width, height] = this.window?.getContentSize() ?? [1280, 800];

  return {
    x: SIDEBAR_WIDTH,
    y: TOOLBAR_HEIGHT,
    width: width - SIDEBAR_WIDTH - PANEL_MARGIN_RIGHT,
    height: height - TOOLBAR_HEIGHT - PANEL_MARGIN_BOTTOM,
  };
}
```

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/src/main/browser/tab-manager.ts
git commit -m "fix: update tab-manager bounds for new panel layout"
```

---

### Task 13: Delete old files and clean up

**Files:**
- Delete: `apps/desktop/src/renderer/components/sidebar.tsx`
- Delete: `apps/desktop/src/renderer/components/address-bar.tsx`
- Delete: `apps/desktop/src/renderer/components/create-profile-dialog.tsx`

- [ ] **Step 1: Delete old component files**

```bash
rm apps/desktop/src/renderer/components/sidebar.tsx
rm apps/desktop/src/renderer/components/address-bar.tsx
rm apps/desktop/src/renderer/components/create-profile-dialog.tsx
```

- [ ] **Step 2: Run typecheck to verify no broken imports**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop 2>&1 | tail -10`

Fix any remaining import issues.

- [ ] **Step 3: Run lint**

Run: `cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/ui 2>&1 | tail -5`

- [ ] **Step 4: Run knip**

Run: `cd /Users/andrevictor/www/pane && bunx knip 2>&1 | tail -20`

Fix any unused exports or dependencies.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: remove old sidebar, address-bar, and create-profile-dialog"
```

---

### Task 14: Visual testing and polish

- [ ] **Step 1: Start the dev server**

Run: `cd apps/desktop && bun run dev`

- [ ] **Step 2: Test the golden path**

1. App opens with the two-tone layout (dark sidebar, lighter content panel)
2. Content panel has rounded corners and shadow
3. Click "+ New" in sidebar footer → sheet slides in from right
4. Create a profile with a color → verify color pip appears in sidebar
5. Open a tab → verify profile gets colored border, address bar badge shows profile color
6. Switch to settings → verify settings page renders inside the content panel
7. Close sheet → verify tabs reappear if on browser page

- [ ] **Step 3: Test edge cases**

1. Empty state shows centered "Pane" wordmark when no tab active
2. Multiple active profiles show different colored borders
3. Collapsed active profile shows tab count badge
4. Window resize → content panel and webviews resize correctly
5. Drag window from titlebar area

- [ ] **Step 4: Fix any visual issues found during testing**

- [ ] **Step 5: Final commit**

```bash
git add -A
git commit -m "style: complete UI redesign - Arc-style panel layout"
```
