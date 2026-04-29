# UI Redesign — Arc-Style Minimal Dark Theme

Refactor Pane's UI to an ultra-minimal dark aesthetic inspired by Claude Code desktop, Linear, and Conductor. Arc-style panel layout with composition pattern components.

## Layout & Structure

### Arc-style panel layout

The entire window background is the sidebar color (`--color-background`). The content area (browser + address bar) is a **raised panel** floating on top of the sidebar background.

- **Window base**: `#0a0a0c` — the sidebar IS the background
- **Content panel**: `#161618` — a raised card with `border-radius: 10px`, `margin: 8px 8px 8px 0`, and shadow `0 0 0 1px rgba(255,255,255,0.06), 0 4px 24px rgba(0,0,0,0.4)`
- **No hard border** between sidebar and content — the panel's rounded edge creates natural separation

### Sidebar (220px)

Three zones, top to bottom:

1. **Titlebar** (44px height, 14px horizontal padding): macOS traffic lights + "Pane" label. Drag region. No buttons here.
2. **Profile list** (flex-1, scrollable, 10px horizontal padding): collapsible profiles with nested tabs.
3. **Footer**: `+ New` ghost button → 1px separator → Settings button.

### Content panel

- **Address bar**: 6px vertical outer padding, 10px horizontal padding. Layout order: `nav buttons → URL input → profile badge → extension icons`. URL input and profile badge share the same height (30px). Border-bottom `1px solid rgba(255,255,255,0.05)`.
- **Web content**: fills remaining space below address bar.
- **Settings page**: renders inside the same content panel, replacing the web content area.

## Color System

All colors use the existing shadcn CSS variable pattern. Updated values:

| Variable | Value | Purpose |
|----------|-------|---------|
| `--color-background` | `#0a0a0c` | Sidebar / window base |
| `--color-foreground` | `#fafafa` | Primary text (unchanged) |
| `--color-card` | `#161618` | Content panel, sheets, popovers |
| `--color-card-foreground` | `#fafafa` | (unchanged) |
| `--color-popover` | `#161618` | Dialogs, dropdowns |
| `--color-popover-foreground` | `#fafafa` | (unchanged) |
| `--color-primary` | `#fafafa` | (unchanged) |
| `--color-primary-foreground` | `#18181b` | (unchanged) |
| `--color-secondary` | `#1c1c1f` | Subtle backgrounds |
| `--color-secondary-foreground` | `#fafafa` | (unchanged) |
| `--color-muted` | `#1c1c1f` | Muted areas |
| `--color-muted-foreground` | `#71717a` | Secondary text (dimmer) |
| `--color-accent` | `#1c1c1f` | Hover states |
| `--color-accent-foreground` | `#e4e4e7` | Slightly softer than foreground |
| `--color-destructive` | `#7f1d1d` | (unchanged) |
| `--color-destructive-foreground` | `#fafafa` | (unchanged) |
| `--color-border` | `rgba(255,255,255,0.06)` | Ultra-subtle borders |
| `--color-input` | `rgba(255,255,255,0.06)` | Input borders |
| `--color-ring` | `#52525b` | Focus rings (subtler) |

### Profile colors

9 preset colors stored per-profile. Not part of the shadcn theme tokens — applied dynamically.

| Name | Hex |
|------|-----|
| Blue | `#60a5fa` |
| Rose | `#fb7185` |
| Emerald | `#34d399` |
| Amber | `#fbbf24` |
| Violet | `#a78bfa` |
| Orange | `#f97316` |
| Teal | `#2dd4bf` |
| Fuchsia | `#e879f9` |
| Zinc | `#a1a1aa` |

Each color is used at three opacities:
- **Solid**: color pip, badge text
- **30–40% opacity**: border on active profiles, badge border
- **4–8% opacity**: background tint on active profiles, badge background

## Sidebar Design

### Profile row

- Chevron (▼/▶) + color pip (4px × 14px rounded bar) + profile name
- **Active profile** (has open tabs): `border: 1px solid {color @ 40%}`, `background: {color @ 4%}`, text `#d4d4d8`
- **Inactive profile**: `border: 1px solid transparent`, text `#71717a`
- Collapsed active profiles show a tab count badge on the right (`#52525b`)

### Tab list

- Indented 10px from parent profile row, 6px top margin
- Each tab: favicon (12px square) + title (11px)
- Active tab: `background: rgba(255,255,255,0.05)`, text `#e4e4e7`
- Inactive tab: no background, text `#71717a`
- Close button (X) appears on hover via `group-hover:opacity-100`
- No vertical connecting line (no `border-left`)

### "New tab" button

- Indented 14px (more than tabs at 10px)
- Ghost style, `#3f3f46` text, `+ New tab`

### Footer

- `+ New` ghost button: `#3f3f46`, opens create profile sheet
- 1px separator: `rgba(255,255,255,0.04)`
- Settings button: `#52525b`, highlighted with `background: rgba(255,255,255,0.05)` when on settings page

## Address Bar

Layout: `nav buttons → URL input → profile badge → extension icons`

- **Nav buttons**: back, forward, reload. 26px square, `#52525b`, ghost style
- **URL input**: `flex: 1`, 30px height, `background: rgba(255,255,255,0.03)`, `border: 1px solid rgba(255,255,255,0.06)`, `border-radius: 5px`, text `#71717a`, 12px
- **Profile badge**: same 30px height as URL input, colored with profile's color. `background: {color @ 8%}`, `border: 1px solid {color @ 30%}`, text in profile color, 11px `font-weight: 500`
- **Extension icons**: 26px square, `#3f3f46`, positioned after the badge

## Sheets (replacing dialogs)

All create/edit/view forms use a **right-side floating sheet** instead of centered dialogs.

- Install `Sheet` from shadcn into `@pane/ui`
- Sheet width: 360px
- Same visual treatment as content panel: `border-radius: 10px`, shadow, `#161618` background
- Positioned `top: 8px; right: 8px; bottom: 8px`
- Overlay dims background with `rgba(0,0,0,0.4)`
- Header: title at 14px `font-weight: 500` + close button
- Body: scrollable form content
- Footer: action buttons pinned to bottom

### Create profile sheet fields

1. Name (text input)
2. Group (optional text input)
3. Color (9-color picker, selected state has double ring)
4. Platform (3 toggle buttons: Windows / macOS / Linux, default to current platform)
5. Separator
6. Enable proxy (switch toggle)
7. Proxy fields when enabled (type select, host, port, username, password)
8. Footer: Cancel (outline) + Create (primary)

## Settings Page

Renders inside the content panel, replacing web content.

- Title: "Settings" at 14px `font-weight: 500`
- Subtitle: `#52525b`, 12px
- Sections grouped by uppercase 10px labels with `letter-spacing: 0.5px` (e.g., BROWSER, APPEARANCE, DATA)
- Subtle 1px separators between sections
- Same input styling as sheets
- Max-width 480px, left-aligned with 32px vertical / 40px horizontal padding

## Empty State

When no tab is active in browser view:

- Content panel renders with its normal rounded corners and shadow
- Centered "Pane" wordmark or muted icon in `#27272a`
- Subtitle "Open a profile to start browsing" in `#3f3f46`, 12px
- No action buttons — the action path is in the sidebar

## Typography

- System font stack: `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif`
- **Sentence case** for all UI labels ("New tab", "New profile", not "New Tab")
- Sheet/page titles: 14px, `font-weight: 500` (medium)
- Profile names: 12px, `font-weight: 500`
- Tab titles: 11px
- Section labels (settings): 10px, uppercase, `letter-spacing: 0.5px`
- Form labels: 11px, `font-weight: 500`

## Component Architecture

All app-level components use the **composition pattern** with named exports. No dot notation. Small props (`variant`, `size`, `className`, `color`, `active`) are fine. Structure comes from composition, not config props.

### Sidebar

```
Sidebar, SidebarHeader, SidebarTitle, SidebarContent, SidebarFooter,
SidebarNewButton, SidebarSeparator, SidebarSettingsButton
```

### Profile & tabs

```
ProfileItem, ProfileName, ProfileBadge, ProfileTabs,
TabItem, TabFavicon, TabTitle, TabNew
```

### Address bar

```
AddressBar, AddressBarNav, AddressBarInput,
AddressBarProfileBadge, AddressBarExtensions
```

These are colocated in the renderer components folder. Shared primitives (`Sheet`, `Button`, `Input`, etc.) come from `@pane/ui` via shadcn.

## Migration Summary

| Current | New |
|---------|-----|
| `Dialog` (centered modal) | `Sheet` (right-side floating panel) |
| Sidebar `border-r` separation | No border — panel edge creates separation |
| Same background everywhere (`#09090b`) | Two-tone: `#0a0a0c` base, `#161618` panel |
| Green dot for active profile | Colored border in profile's color |
| `h-10` address bar, 14px padding | Tight address bar, 6px padding |
| `+` button in sidebar header | `+ New` ghost button in footer |
| Tree line (`border-l`) on tabs | No line, 10px indent |
| Neutral gray profile badge | Badge colored to match profile |
| Monolithic component files | Composition pattern with named exports |
| Title case UI labels | Sentence case |
