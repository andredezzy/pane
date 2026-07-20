# Pane

Multi-profile desktop browser with per-profile proxies, fingerprint spoofing, and Chrome extension support. Pane's own UI (the chrome) wraps profile tabs that render real websites.

## Language

**Update check**:
Comparing the running app's version against the repo's latest GitHub Release. Yields at most one available update — the newest release — never a list of missed versions. Installing remains a user action (download + drag) while Pane is unsigned.
_Avoid_: auto-update (implies silent install, which unsigned macOS forbids)

**Theme**:
The app-wide appearance preference — `system`, `light`, or `dark`. One coherent knob: Pane's chrome, native menus, and what websites in profile tabs observe via `prefers-color-scheme` all follow it, matching desktop Chrome's appearance setting. `system` follows macOS.
_Avoid_: appearance, color mode, dark mode (as a feature name)
