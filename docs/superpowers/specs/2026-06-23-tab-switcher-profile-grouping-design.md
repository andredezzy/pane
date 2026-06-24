# Tab Switcher Profile Grouping Design

## Goal

Reorganize the Ctrl+Tab MRU switcher so its tabs are grouped under their profile and each group shows the profile name. Today the switcher is a flat most-recently-used list where each row carries a small profile color dot, which makes it hard to tell which profile a tab belongs to when several profiles are interleaved.

All changes stay inside `src/renderer/components/tab-switcher.tsx`. No store, IPC, or data-model changes.

## Scope

- The switcher still shows only the **~8 most-recently-used tabs** (`MAX_VISIBLE_TABS = 8`, counting tabs, not headers). It stays a lightweight quick-switcher, not a full tab overview.
- Those tabs are now **grouped by profile** with a profile-name header per group.

## Grouping & Ordering

`resolveMruTabs()` is replaced by `resolveMruGroups()`:

1. Walk `mruHistory` in order, resolving each tab id to its owning profile (same lookup as today).
2. Bucket tabs into **profile groups** keyed by profile id. A profile's group appears in **first-seen order** — i.e. ordered by the recency of that profile's most-recent tab.
3. Tabs within a group stay in `mruHistory` order (recency).
4. Stop once 8 tabs total have been collected.

Resulting shape:

```typescript
interface SwitcherTab {
  id: string;
  title: string;
  favicon: string;
}

interface ProfileGroup {
  id: string;
  name: string;
  color: ProfileColor;
  tabs: SwitcherTab[];
}
```

## Selection & Keyboard

- Selection runs over the **flattened** tab order: `groups.flatMap((group) => group.tabs)`. Headers are never selectable.
- `Ctrl+Tab` / `Ctrl+Shift+Tab` advance `selectedIndex` forward/backward over the flattened list and wrap, exactly as today.
- **Quick-flick preserved:** initial `selectedIndex` = the flattened position of `mruHistory[1]` (the previous tab), so a single Ctrl+Tab still lands on it wherever its group now sits. Fallback to `Math.min(1, flattened.length - 1)` if that id isn't present.
- Release of `Control` confirms the selected tab (`trpc.tabs.switch`); `Escape` cancels. Mouse click (`onMouseDown`) still confirms a row. Mouse hover does nothing (already removed).

## Visual Design

The per-row color dot is redundant once tabs are grouped, so profile identity (color + name) moves into a non-interactive **group header**, and tab rows indent beneath it.

```
╭────────────────────────────────────╮
│  ● Personal                         │   header: color dot + profile name
│      M   Gmail                      │
│      ▽   Proton Mail                │   tab row: favicon + title, indented
│                                     │
│  ● Work                             │
│    ┌──────────────────────────────┐│
│    │ M   Caixa de entrada…        ││   ← selected (bg-white/10)
│    └──────────────────────────────┘│
│                                     │
│  ● MagicPay                         │
│      ◇   Email – Outlook…           │
╰────────────────────────────────────╯
```

- **Header** — `dot (h-2 w-2 rounded-full, profile color)` + name at `text-[11px] font-medium text-white/40`, natural case (no forced uppercase — matches the sentence-case label convention). Non-interactive `div`, truncates if long.
- **Tab row** — `button`, favicon (`h-4 w-4`, or `bg-white/10` placeholder) + `text-sm text-white/80` truncated title. Left-indented so the favicon aligns under the header name (containment without nesting lines). Per-row color dot removed. Selected row keeps `bg-white/10`; no per-row hover.
- **Spacing** — groups separated by a vertical gap; rows tight within a group. Container chrome unchanged (`w-[320px] rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl`, centered with a dimmed backdrop).

## Edge Cases

- **Single profile** — still renders one header (consistent, and the profile name is wanted).
- **Tab with no matching profile** — skipped, as today.
- **Empty list** — `onClose()`, as today.

## File Changes Summary

### Modified files
- `src/renderer/components/tab-switcher.tsx` — replace `resolveMruTabs()` with `resolveMruGroups()`, render grouped headers + indented tab rows, flatten for selection, and seed the initial selection from `mruHistory[1]`'s flattened position.
