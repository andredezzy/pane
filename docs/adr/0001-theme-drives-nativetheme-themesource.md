# Theme drives nativeTheme.themeSource

The theme setting (`system` / `light` / `dark`) is applied to Electron's `nativeTheme.themeSource`, so one knob drives Pane's chrome, native menus and dialogs, AND the `prefers-color-scheme` every website in every profile observes — the same coupling desktop Chrome's appearance setting has. Electron offers no way to split them: `themeSource` is global to native UI and all web contents.

We first considered theming only Pane's chrome and leaving websites on the real OS scheme (arguably "honest" for fingerprinting), but rejected it because native context menus would then mismatch the chrome whenever the theme differs from the OS — and since real Chrome exposes its appearance setting to websites the same way, following it is Chrome parity, not a fingerprint anomaly. The renderer relies on this coupling: its CSS tokens are `light-dark()` pairs keyed off `prefers-color-scheme`, with no theme class plumbing.
