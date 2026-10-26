# Human checklist

Items the autonomous run could not do or verify on its own: each says why it needs a person, the exact steps, and the evidence file the result should produce. Open items first; done items move to the end with the date.

## Open

- **Phase 1 · Acceptance (window screenshot, accessibility read, Cmd+Q) — unlock the Mac.** The screen has been locked since ~04:20 on 2026-10-26, and macOS hides windows from accessibility and refuses input while locked, so these three checks could not run. Nothing to do but unlock: the agent re-runs the check as soon as it sees the session unlocked. To run it by hand:
  ```bash
  bun native/engine/tests/run.ts --binary native/app/build/Pane.app/Contents/MacOS/Pane --out native/evidence/phase-1/scenario hosted
  ```
  Expected: `native/evidence/phase-1/scenario/hosted.json` with `"visual": "verified"`, plus `window.png` showing example.com inside the Pane window.

- **Phase 2 · parity.md — review the Electron-only rows marked ⛔.** They are internals of the Electron architecture with no user-facing equivalent to port: `stores.push`/`stores.sync` (cross-process state sync), `tabs.hideAll`/`showActive` and `ui.present`/`dismiss` (an overlay window above page views; native sheets draw above pages), and the app-UI DevTools shortcut (⌥⌘I; the app UI is native). Nothing to do unless one of them should stay; the justification is in each row of `docs/native/parity.md`.

## Done

- **Phase 0 · 0.1 — Install full Xcode and select it** (2026-10-26). Xcode 27.1 installed and selected with `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
