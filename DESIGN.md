# Pane Design Guidelines

Interaction and visual principles for Pane. Keep entries short and prescriptive.

## Motion

### Keyboard-driven changes animate; pointer-driven changes are instant

Any state change caused by **keyboard input** must animate with a transition, so the eye can follow a change it did not physically point at. This covers keyboard navigation and selection — cycling the Ctrl+Tab switcher, arrow-key menus, focus moving between controls, and similar.

Direct **pointer** actions (click/tap) apply instantly. The user already knows what they touched, so a fade only adds latency. For example: clicking a tab in the sidebar switches its selected background with no transition, while cycling tabs with Ctrl+Tab animates the moving highlight.

Implementation notes:

- Use the default `transition` utility for selection highlights, not `transition-colors` — rings are `box-shadow`, which `transition-colors` does not cover, so the fill and the ring must animate together.
- Always honor `prefers-reduced-motion`.
