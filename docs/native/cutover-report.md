# Cutover report: the native Pane

The native macOS Pane replaces the Electron app (`apps/veil`) on the `native-shell` branch. It is a Swift app (AppKit and SwiftUI) on Pane's own Chromium framework, `PaneCore.framework`, embedded in-process as Arc and Dia embed theirs (`docs/adr/0004-native-shell-on-own-chromium-framework.md`). The engine is Chromium 157.0.8103.15, the current stable milestone. `main` keeps the Electron app until the branch is merged.

Plan: `docs/superpowers/plans/2026-10-26-native-shell-arc-architecture.md`. Engineering log: `native/DECISIONS.md`. Evidence: `native/evidence/phase-N/` (not committed).

## Parity

`docs/native/parity.md` lists every user-facing capability of the Electron app, and where the native app delivers it.

| Status | Rows | Meaning |
|---|---|---|
| ✅ | 91 | Done, with evidence |
| 🟡 | 8 | Built, and proven in the model. What remains is an on-screen check with the real pointer |
| dropped | 6 | Removed by the user's decision |
| ⛔ | 4 | Internals of the Electron architecture, with nothing to port |

The whole scenario suite (50 scenarios) passes on the final engine: `native/evidence/phase-6/report.json`.

### Built, waiting for an on-screen check

Each of these needs the real pointer: drags, right clicks, and pickers. CUA moves the mouse for them, which would fight anyone using the Mac. They run when the Mac is unlocked and idle (`native/HUMAN-CHECKLIST.md`, Phase 2).

- `ui.menu`, the profile context menu, and the tab context menu: SwiftUI context menus on the sidebar rows.
- `ui.confirm`: the delete-profile confirmation.
- Sidebar drag to reorder profiles and tabs. The reorder model is proven (`phase-2/restore.json`).
- Profile sheet: the color and group pickers. The name, Create, and Save were driven on screen (`phase-5/profile-sheet.json`).
- ⌃⌘F full screen (View menu). A page's own full screen is proven (`fullscreen` scenario).
- The Edit menu: standard responder actions, left unexercised to keep the user's clipboard untouched.

### Dropped, and why

By the user's decision (2026-10-26): the Google sign-in fallback was a workaround for Google rejecting the Electron engine, and a browser does not need one. Google accepts the native engine with and without a fingerprint (`phase-5/google.json`). Dropped with it:

- The Google sign-in fallback itself (real Chrome and a cookie import).
- `profiles.signOut`, Sign out of Google in the profile menu.
- `profiles.signedIn`, which only fed Sign out of Google.
- `settings.detectBrowser` and `chromiumPath`, which found Chrome for the fallback. `chromiumPath` still round-trips in `settings.json`.
- Settings › Browser (the Chrome path and auto-detect).

Electron internals, with no user-facing equivalent:

- `stores.push` / `stores.sync`: state sync between Electron's processes. The native app is one process with one state.
- `tabs.hideAll` / `showActive`: hid pages while an overlay was up, because Electron's overlays could not draw above page views. Native sheets and popovers draw above pages.
- `ui.present` / `dismiss`: the overlay window for sheets, replaced by native sheets and popovers.
- App DevTools (⌥⌘I): the app's UI is native, so it has no web UI to inspect.

## What changed on purpose

These are not missing features. Each is recorded in `native/DECISIONS.md`.

- **Data moves over once.** The first launch copies the Electron app's data (`~/Library/Application Support/@pane/veil`), never touching the original: profiles, tabs, settings, the PIN, and each profile's cookies and storage. Extensions stay behind: the Electron app installed each for every profile, and a native profile has only the ones added to it. The cookie key comes from the Electron app's Keychain item, so macOS asks once.
- **Chrome makes no requests of its own**: no component updater, network time, AI Mode eligibility, Translate ranker, or Google account list. The `quiet` scenario sees nothing leave but the pages' own traffic.
- **Chrome 157's own changes**: a new extension is pinned to the toolbar, as every extension's button showed in the Electron app. A page goes fullscreen only from a focused view.
- **One version source**: `native/app/VERSION`. The bundle's version keys, Settings, the update check, and the dmg's name all come from it. The bundle carries Pane's icon.
- **Apple Silicon Macs only.** The Electron app's Windows, Linux, and Intel Mac builds end with it (plan Decision 4). Its update check already offered only the arm64 dmg.
- **The release is the Electron app's**, on the same update contract: `Pane-<version>-arm64.dmg`, laid out by the same dmgbuild and settings, ad-hoc signed until a Developer ID exists. The update check reads the same GitHub releases.

## Maintenance

- **Chromium upgrades** follow `native/engine/UPGRADING.md`, proven by moving from 156.0.8078.48 to 157.0.8103.15 (`phase-6/upgrade.log`).
- **Prebuilt engines** (`native/engine/prebuilt-engines.tsv`) let the app build without a Chromium checkout. CI builds it that way (`.github/workflows/native.yml`), and the release workflow (`.github/workflows/release.yml`) packages it.
- **Signing and notarization** are built and wait for a Developer ID (`PANE_SIGN_IDENTITY`, `PANE_NOTARY_PROFILE`). Sparkle waits for them.

## Open items for a person

From `native/HUMAN-CHECKLIST.md`. Each entry there has its steps and the evidence it should produce.

- **Phase 2 · Pointer gestures on screen**: the 🟡 rows above. Leave the Mac unlocked and idle.
- **Phase 3 · Mail**: check Drafts for a stray empty draft to `pane-scenario@example.invalid`.
- **Phase 3 · Pick a file through the open panel**, by hand, once.
- **Phase 4 · Sign in to 1Password and NordPass**, and autofill a login form.
- **Phase 5 · Sign in to a real Google account.**
- **Phase 5 · Migrate your Electron Pane**, and allow its Keychain item.
- **Phase 6 · Sign and notarize with a Developer ID**, then Sparkle.
- **Phase 6 · The first CI run**: publish the engine prerelease and push.
- **Reviews**: Picture-in-picture uses Chromium's player, and the ⛔ rows of `parity.md`.
- **Publishing the first native release**: the last entry of the checklist.
