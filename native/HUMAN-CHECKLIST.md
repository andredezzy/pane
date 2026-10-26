# Human checklist

Items the autonomous run could not do or verify on its own: each says why it needs a person, the exact steps, and the evidence file the result should produce. Open items first; done items move to the end with the date.

## Open

- **Phase 1 · Acceptance (window screenshot, accessibility read, Cmd+Q) — unlock the Mac.** The screen has been locked since ~04:20 on 2026-10-26, and macOS hides windows from accessibility and refuses input while locked, so these three checks could not run. Nothing to do but unlock: the agent re-runs the check as soon as it sees the session unlocked. To run it by hand:
  ```bash
  bun native/engine/tests/run.ts --binary native/app/build/Pane.app/Contents/MacOS/Pane --out native/evidence/phase-1/scenario hosted
  ```
  Expected: `native/evidence/phase-1/scenario/hosted.json` with `"visual": "verified"`, plus `window.png` showing example.com inside the Pane window.

- **Phase 2 · On-screen checks — unlock the Mac.** Same cause as above. While locked, the switcher was proven from the app's state and an offscreen render of its views (`native/evidence/phase-2/switcher.png`), and zoom from the engine's zoom level (the page is occluded, so its `devicePixelRatio` lags). The agent re-runs these as soon as the session is unlocked; by hand:
  ```bash
  bun native/engine/tests/run.ts --binary native/app/build/Pane.app/Contents/MacOS/Pane --out native/evidence/phase-2/unlocked switcher shortcuts hosted
  ```
  Expected: `switcher.json` with `"screenshot": "switcher-screen.png"` (a real screenshot of the switcher over the window), and `shortcuts.json` with `"pageVisible": true` in each zoom step.

  The agent also drives these with CUA once unlocked, since they are gestures or windows only the screen shows (parity rows marked 🟡): dragging profiles and tabs in the sidebar, the profile and tab context menus, the delete-profile confirmation, the profile sheet, ⌃⌘F full screen, the rendered tab rows and the page inside its card. Nothing to do but unlock.

- **Phase 2 · parity.md — review the Electron-only rows marked ⛔.** They are internals of the Electron architecture with no user-facing equivalent to port: `stores.push`/`stores.sync` (cross-process state sync), `tabs.hideAll`/`showActive` and `ui.present`/`dismiss` (an overlay window above page views; native sheets draw above pages), and the app-UI DevTools shortcut (⌥⌘I; the app UI is native). Nothing to do unless one of them should stay; the justification is in each row of `docs/native/parity.md`.

## Done

- **Phase 0 · 0.1 — Install full Xcode and select it** (2026-10-26). Xcode 27.1 installed and selected with `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
