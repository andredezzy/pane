# Pane Design Guidelines

Interaction and visual principles for Pane. Keep entries short and prescriptive.

## Motion

### Selection is instant; hover and enter/exit animate

Moving a **selection or highlight** applies instantly, with no transition — whether driven by a pointer (clicking a sidebar tab) or by rapid keyboard input (cycling the Ctrl+Tab switcher). A per-step transition lags behind fast input and makes the selection feel sluggish, so the highlight jumps straight to its target.

Reserve transitions for **hover feedback and enter/exit** — smooth, non-repeated changes where motion aids the eye without fighting input speed. For example, an inactive sidebar tab fades its background on hover, but the active (selected) tab switches instantly.

Implementation notes:

- Selection highlights (active/selected backgrounds and rings) carry no `transition` utility.
- Scope `transition-colors` to hover states only — e.g. the inactive branch of a tab's className, never the active/selected branch.
- Always honor `prefers-reduced-motion`.
