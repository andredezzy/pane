# Profile Context Menu + Unified Profile Sheet

## Overview

Add right-click context menu on sidebar profiles with "Edit" and "Delete" actions. Refactor the profile creation sheet into a unified sheet that handles both creation and editing. Extend the surface system with type-safe prop passing. Remove unused model fields.

## 1. YAGNI Cleanup — Remove `notes` and `tags`

**Files:** `stores/profile-store.ts`, `sheets/create-profile.tsx`

Remove `notes` and `tags` from:

- `BrowserProfile` interface — delete both fields
- `CreateInput` type — inherits removal from `BrowserProfile`
- `profileStore.create` call in the sheet — remove `notes: null` and `tags: []`
- Persistence `merge` logic — remove any handling of these fields

These fields are never exposed in the UI and are hardcoded to `null`/`[]` on creation.

## 2. Type-safe Surface System

**Files:** `renderer/surface.ts`, `renderer/app/surface.tsx`, `main/trpc/routers/ui.ts`

### `surface.ts`

Extend `surface.open` with a generic that infers allowed props from the component:

```typescript
open<C extends ComponentType<any>>(
  component: C,
  ...args: SurfaceArgs<C>
): void
```

Where `SurfaceArgs<C>` resolves to `[]` when the component only has `onClose`, and `[props: Omit<ComponentProps<C>, 'onClose'>]` when it has additional props. This makes the second argument required only when the component expects props beyond `onClose`.

The `props` object is serialized as JSON alongside the component name through the existing `postMessage` pipeline.

### `ui.ts` (tRPC router)

Extend the `present` mutation input to accept an optional `props` field (`z.record(z.unknown()).optional()`). Forward it in the `postMessage` payload.

### `surface.tsx` (SurfaceLayout)

Read `props` from the message event data. Spread onto the resolved component alongside `onClose`:

```tsx
<Component key={active.key} {...active.props} onClose={surface.close} />
```

## 3. Profile Store — Add `update` Method

**File:** `stores/profile-store.ts`

Add to `ProfileState` interface:

```typescript
update: (id: string, input: Partial<CreateInput>) => void
```

Implementation: find profile by `id`, shallow-merge the input fields, set `updatedAt` to current ISO timestamp. Same `set()` pattern as existing methods.

## 4. Unified `ProfileSheet` (replaces `CreateProfileSheet`)

**File:** rename `renderer/sheets/create-profile.tsx` → `renderer/sheets/profile-sheet.tsx`

### Props

```typescript
interface Props {
  onClose: () => void;
  profileId?: string;
}
```

- `profileId` absent → create mode
- `profileId` present → edit mode

### Behavior by mode

| Aspect | Create | Edit |
|--------|--------|------|
| Title | "New profile" | "Edit profile" |
| Default values | Empty / detected platform | Populated from store |
| Submit action | `profileStore.create(...)` | `profileStore.update(id, ...)` |
| Submit button label | "Create" | "Save" |

The form schema, fields, and layout are identical in both modes. The only differences are data source and submit handler.

### Reading profile data in edit mode

Read from `profileStore.getState().profiles.find(p => p.id === profileId)` at component mount to populate `defaultValues`. The store is already synced across windows via the `sync` middleware.

For proxy fields: if the profile has a non-null `proxy`, set `proxyEnabled: true` and spread proxy fields. Otherwise use the same empty defaults as create mode.

## 5. Context Menu Primitive in `@pane/ui`

**Files:** `packages/ui/src/components/context-menu.tsx`, `packages/ui/package.json`

### Dependency

Add `@radix-ui/react-context-menu` to `@pane/ui`.

### Component structure

Follow the existing shadcn pattern with named exports (no dot notation per CLAUDE.md):

- `ContextMenuRoot`
- `ContextMenuTrigger`
- `ContextMenuContent`
- `ContextMenuItem`
- `ContextMenuSeparator`

Style consistently with existing `@pane/ui` components.

## 6. Context Menu on Profile Items

**File:** `renderer/app/layout.tsx`

In `SidebarProfileItem`, wrap `ProfileItem` with `ContextMenuRoot` + `ContextMenuTrigger`. The `ContextMenuContent` contains:

- **Edit profile** — calls `surface.open(ProfileSheet, { profileId: profile.id })`
- Separator
- **Delete profile** — styled destructive, calls `trpc.profiles.remove.mutate({ profileId: profile.id })`

## 7. Remove Trash Icon from Profile Header

**File:** `renderer/app/layout.tsx`

Remove the `Trash2` hover icon and its width-animation wrapper from `SidebarProfileItem`. Delete is now exclusively in the context menu. Remove the `Trash2` import if no longer used.

## 8. Update References

- `SidebarNewButton` onClick in `Layout`: change `surface.open(CreateProfileSheet)` → `surface.open(ProfileSheet)`
- Update the import from the renamed file
- Remove old `create-profile.tsx` file (handled by rename)

## Scope Summary

| Change | Files touched |
|--------|--------------|
| Remove `notes`/`tags` | `profile-store.ts` |
| Type-safe surface props | `surface.ts`, `surface.tsx`, `ui.ts` (tRPC) |
| Add `update` to store | `profile-store.ts` |
| Unified `ProfileSheet` | `profile-sheet.tsx` (renamed) |
| Context menu primitive | `context-menu.tsx` (new), `package.json` |
| Context menu on profiles | `layout.tsx` |
| Remove trash icon | `layout.tsx` |
| Update references | `layout.tsx` |
