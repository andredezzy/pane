# Pane Design Guidelines

Interaction and visual principles for Pane. Keep entries short and prescriptive.

## Theme

### One knob, everything follows, instantly

The theme (`system` / `light` / `dark`) sets the app's appearance (`NSApp.appearance`): Pane chrome, native menus, and web content flip together. Switches apply instantly — no cross-fade. Dialog and sheet scrims stay black-alpha in both themes; colors on fixed-color surfaces (profile swatches, status buttons) don't follow the theme.

## Shape

### Corners are superellipses

Every rounded corner is a superellipse: `corner-shape: superellipse(2)` in Paper (CSS's squircle), `Squircle` in the app. The radii are 4, 7, 9, 11, 15, 18, 22, and 29, each 1.84× the circular radius it replaced, so a corner is as deep at 45° as the circle was and flatter where it meets the edges. Circles and capsules stay round: dots, PIN keys, switches, pill bars.

Each radius has its uses (Paper's `--radius-*` tokens carry them too):

- **4** (`--radius-xs`): favicons, letter tiles and badges on a toolbar action, the active profile's square.
- **7** (`--radius-sm`): icon buttons, a menu's letter tiles, a dialog's fields.
- **9** (`--radius-tab`): the address bar, chips.
- **11** (`--radius-md`): buttons, fields, color swatches.
- **15** (`--radius-lg`): sidebar rows, menu rows, the find bar, extension cards, the popup and keyed buttons.
- **18** (`--radius-panel`): the content panel, the profile sheet, the PIN screen's panel.
- **22** (`--radius-xl`): menus, the tab switcher, Settings' cards.
- **29** (`--radius-2xl`): dialogs (a page's, Quit Pane?, an extension's prompt).

Implementation notes:

- A view takes `Squircle(cornerRadius:)`, never a circular `RoundedRectangle`, and one of the scale's radii.
- A ring around a shape is the one exception: it stays even all the way round by taking the shape's radius grown by its offset, through the circle (`Squircle.circularEquivalent`). The profile sheet's selected color swatch, 11, has its rings at 15 and 17.
- A capsule takes `Capsule(style: .circular)`. The default style drew a flat stretch at each end of the 28-point profile chip.
- A layer, which draws only circular or continuous corners (the page view), takes 0.57 of the radius as a circle: it follows the superellipse within a fifth of a pixel, with no mask to composite.

## Motion

### Selection is instant; hover and enter/exit animate

Moving a **selection or highlight** applies instantly, with no transition — whether driven by a pointer (clicking a sidebar tab) or by rapid keyboard input (cycling the Ctrl+Tab switcher). A per-step transition lags behind fast input and makes the selection feel sluggish, so the highlight jumps straight to its target.

Reserve transitions for **hover feedback and enter/exit** — smooth, non-repeated changes where motion aids the eye without fighting input speed. For example, an inactive sidebar tab fades its background on hover, but the active (selected) tab switches instantly.

### An indicator's own change of shape animates

When an item becomes selected and its indicator changes shape, the shape morphs instead of jumping: the active profile's bar grows into a square, rounding off as it widens, and the name beside it moves along. It takes a short spring (about a third of a second, no overshoot to speak of). This is not the highlight moving from item to item, which stays instant. It is the item's own detail settling into its new state, and it is where the app's craft shows.

Implementation notes:

- Selection highlights (active/selected backgrounds and rings) carry no `transition` utility.
- An indicator that changes shape with a selection animates its size and radius together (`Squircle`'s radius animates), with the spring `Motion.morph`.
- Hover fills animate in 0.15 s, easing out, everywhere a hover shows: a fill that appears at once is a missing animation.
- Scope `transition-colors` to hover states only — e.g. the inactive branch of a tab's className, never the active/selected branch.
- Always honor `prefers-reduced-motion`.

## Updates

### Updates surface quietly; installs stay manual

Availability is announced, never enforced: a quiet pill in the sidebar footer and an "Updates" section in Settings — never a modal, never a forced restart, never applied automatically. The pill is dismissible per launch per version, and reappears if a newer version ships later.

The app is ad-hoc signed, not notarized, so it can't safely replace itself in place. Downloading fetches the dmg and opens it so Finder mounts it — installing is always the user dragging Pane to /Applications themselves.

Implementation notes:

- The pill and the Settings section both read the shared `update-store` status (`CHECKING`, `AVAILABLE`, `DOWNLOADING`) — never introduce a second source of truth for update state.
- Dismissing the pill is in-memory only (no persistence); it resets on relaunch.
