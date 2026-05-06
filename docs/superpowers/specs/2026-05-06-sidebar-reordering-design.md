# Sidebar Reordering — Design Spec

## Overview

Add drag-and-drop reordering for profiles in the sidebar and tabs within each profile. Uses `@dnd-kit/core` + `@dnd-kit/sortable` + `@dnd-kit/modifiers`.

## Scope

- Drag-and-drop only (no keyboard reorder, no context menu)
- Vertical axis-locked dragging
- Tabs reorder within the same profile only (no cross-profile drag)
- Persisted automatically via existing `sync` + `persist` middleware

## Store Layer

File: `apps/veil/src/stores/profile-store.ts`

Two new actions added to the profile store:

### `reorderProfiles(activeId: string, overId: string): void`

Finds both profiles by ID in the `profiles[]` array, splices the active profile out, and inserts it at the over profile's position. Uses IDs (not indices) because `@dnd-kit` works with string identifiers.

### `reorderTabs(profileId: string, activeId: string, overId: string): void`

Same splice logic but scoped to a single profile's `tabs[]` array.

No new tRPC routes are needed — the existing `sync` middleware auto-propagates any `setState` call to the main process via `stores.push` / `stores.sync`.

## Component Architecture

Two independent `DndContext` + `SortableContext` setups. They do not nest inside each other's drag logic.

### Profile Reordering

Location: `apps/veil/src/renderer/app/layout.tsx`

- Wrap the `profileIds.map(...)` section with `DndContext` + `SortableContext`
- Each `SidebarProfileItem` becomes sortable via the `useSortable` hook
- `onDragEnd` calls `reorderProfiles(activeId, overId)`
- Sorting strategy: `verticalListSortingStrategy`

### Tab Reordering

Location: `apps/veil/src/renderer/app/layout.tsx` (inside expanded profile section)

- Each expanded profile's tab list gets its own `DndContext` + `SortableContext`
- Each `TabItem` becomes sortable via `useSortable`
- `onDragEnd` calls `reorderTabs(profileId, activeId, overId)`
- Sorting strategy: `verticalListSortingStrategy`

### Sensors

Both contexts use `PointerSensor` with `activationConstraint: { distance: 5 }`. The 5px threshold prevents drag from triggering on normal clicks (profile expand/collapse, tab switch).

### Drag Overlay

Both contexts use `DragOverlay` from `@dnd-kit` to render a styled clone of the dragged item. The original stays in place with reduced opacity (~0.4). The overlay gets a subtle shadow and slight scale-up (~1.02).

### Axis Lock

Both contexts use the `restrictToVerticalAxis` modifier from `@dnd-kit/modifiers`.

## Visual Feedback

- **Dragged item:** `DragOverlay` clone with shadow + 1.02 scale
- **Original position:** Reduced opacity (0.4) to indicate "being moved"
- **Remaining items:** Animate into new positions via `@dnd-kit/sortable`'s built-in CSS transform transitions
- **Cursor:** `grab` on hover, `grabbing` during active drag
- **Drop indicator:** No explicit line — items shift to create a natural gap at the drop position

## Dependencies to Add

Install in `apps/veil`:

- `@dnd-kit/core`
- `@dnd-kit/sortable`
- `@dnd-kit/modifiers`

## Files Changed

| File | Change |
|------|--------|
| `apps/veil/package.json` | Add `@dnd-kit` dependencies |
| `apps/veil/src/stores/profile-store.ts` | Add `reorderProfiles` and `reorderTabs` actions |
| `apps/veil/src/renderer/app/layout.tsx` | Wrap profile and tab lists with DnD contexts, add `onDragEnd` handlers, add `DragOverlay` |
| `apps/veil/src/renderer/components/sidebar/profile-item.tsx` | Wire `useSortable` into `ProfileItem` / `ProfileHeader` |
| `apps/veil/src/renderer/components/sidebar/tab-item.tsx` | Wire `useSortable` into `TabItem` |
