# Theme drives the app and every page

> **On the Views line** (`docs/plans/views-migration.md`, H6): the Theme is Chromium's app-level color scheme (`ui::NativeTheme`'s override, `pane/ui/design/theme.cc`), not `NSApp.appearance`: each window's colors, its NSWindow's appearance, which the menus it opens take, and every page follow it. Settings' Theme menu sets it; a change spends about 7 ms in Chromium's color machinery before its frame (every color provider destroyed and built again with Chrome's mixers, and each page's color maps).

The theme setting (`system` / `light` / `dark`) sets the app's appearance (`NSApp.appearance`), which Chromium's native theme follows, so one knob drives Pane's chrome, native menus and dialogs, AND the `prefers-color-scheme` every website in every profile observes — the same coupling desktop Chrome's appearance setting has. The Electron app made the same choice through `nativeTheme.themeSource`, which could not split them either.

We first considered theming only Pane's chrome and leaving websites on the real OS scheme (arguably "honest" for fingerprinting), but rejected it because native context menus would then mismatch the chrome whenever the theme differs from the OS — and since real Chrome exposes its appearance setting to websites the same way, following it is Chrome parity, not a fingerprint anomaly.
