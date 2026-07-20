# Pane Design Guidelines

Interaction and visual principles for Pane. Keep entries short and prescriptive.

## Theme

### One knob, everything follows, instantly

The theme (`system` / `light` / `dark`) drives `nativeTheme.themeSource`: Pane chrome, native menus, and web content flip together (see docs/adr/0001). Switches apply instantly — no cross-fade. Dialog and sheet scrims stay black-alpha in both themes; colors on fixed-color surfaces (profile swatches, status buttons) don't follow the theme.

## Motion

### Selection is instant; hover and enter/exit animate

Moving a **selection or highlight** applies instantly, with no transition — whether driven by a pointer (clicking a sidebar tab) or by rapid keyboard input (cycling the Ctrl+Tab switcher). A per-step transition lags behind fast input and makes the selection feel sluggish, so the highlight jumps straight to its target.

Reserve transitions for **hover feedback and enter/exit** — smooth, non-repeated changes where motion aids the eye without fighting input speed. For example, an inactive sidebar tab fades its background on hover, but the active (selected) tab switches instantly.

Implementation notes:

- Selection highlights (active/selected backgrounds and rings) carry no `transition` utility.
- Scope `transition-colors` to hover states only — e.g. the inactive branch of a tab's className, never the active/selected branch.
- Always honor `prefers-reduced-motion`.

## Updates

### Updates surface quietly; installs stay manual

Availability is announced, never enforced: a quiet pill in the sidebar footer and an "Updates" section in Settings — never a modal, never a forced restart, never applied automatically. The pill is dismissible per launch per version, and reappears if a newer version ships later.

The app is ad-hoc signed, not notarized, so it can't safely replace itself in place. Downloading fetches the dmg and opens it so Finder mounts it — installing is always the user dragging Pane to /Applications themselves.

Implementation notes:

- The pill and the Settings section both read the shared `update-store` status (`CHECKING`, `AVAILABLE`, `DOWNLOADING`) — never introduce a second source of truth for update state.
- Dismissing the pill is in-memory only (no persistence); it resets on relaunch.
