# Human checklist

Items the autonomous run could not do or verify on its own: each says why it needs a person, the exact steps, and the evidence file the result should produce. Open items first; done items move to the end with the date.

## Open

- **Phase 2 · Pointer gestures on screen — run when you are away from the Mac.** Dragging profiles and tabs in the sidebar, the profile and tab context menus, the delete-profile confirmation, the profile sheet, and ⌃⌘F full screen need the real pointer (CUA moves the mouse for drags), which would fight you while you work. The rendered rows and the page in its card are now proven on screen (`native/evidence/phase-2/unlocked/`). Nothing to do but leave the Mac unlocked and idle; the agent runs them then.

- **Phase 3 · Mail — look for a stray draft.** A scenario opened Mail at 07:08 on 2026-10-26 with an unsent compose window to `pane-scenario@example.invalid` (a `mailto:` link, which Chrome opens without asking); Mail was quit two minutes later and nothing was sent. If Mail autosaved that window, an empty draft to that address may be in Drafts: delete it. Details in `native/DECISIONS.md` (Phase 3, Incident).

- **Phase 3 · Pick a file through the open panel, by hand.** macOS 27 runs open and save panels out of process, and the scenarios answer them beside the panel. Once: open a page with `<input type=file>` in Pane (any upload form), choose a file in the panel, and check the page shows its name.

- **Phase 4 · Sign in to 1Password and NordPass, and autofill a login form.** Both install from the Web Store and their toolbar buttons open their own pages (`native/evidence/phase-4/1password.png`, `nordpass.png`; until you sign in, each opens its welcome page in a tab, as in Chrome). Signing in uses your accounts, so it is yours: in a Pane profile, sign in from each manager's button, then open `native/engine/tests/pages/password.html` (served by any scenario run, or open the file) and check each one offers to fill the form.

- **Phase 3 · Picture-in-picture uses Chromium's player — review.** The floating video player is Chromium's own window (the only surface that can show the video's compositor frame; AVKit's picture-in-picture plays AVFoundation media only). Nothing to do unless it should look different; the reasoning is in `native/DECISIONS.md`.

- **Phase 2 · parity.md — review the Electron-only rows marked ⛔.** They are internals of the Electron architecture with no user-facing equivalent to port: `stores.push`/`stores.sync` (cross-process state sync), `tabs.hideAll`/`showActive` and `ui.present`/`dismiss` (an overlay window above page views; native sheets draw above pages), and the app-UI DevTools shortcut (⌥⌘I; the app UI is native). Nothing to do unless one of them should stay; the justification is in each row of `docs/native/parity.md`.

## Done

- **Design · Paper access** (2026-10-26). The Paper app was installed but closed; opened in the background, its local MCP server gave read access to the Pane file, and the native UI was matched to it and to the Electron app (`native/evidence/design/`, DECISIONS: Design fidelity).

- **Phase 1 · Acceptance on screen** (2026-10-26). The Mac was unlocked: `hosted` passed with a real window screenshot, the accessibility read, and Quit Pane from the menu bar (`native/evidence/phase-1/unlocked/`).
- **Phase 2 · On-screen checks** (2026-10-26). `switcher` with a real screenshot of the switcher (`switcher-screen.png`) and `shortcuts` with the page visible at each zoom step (`native/evidence/phase-2/unlocked/`). Pointer gestures remain (Open).
- **Phase 3 · On-screen checks** (2026-10-26). Every sheet and panel answered with a CUA click, spelling suggestions replacing a misspelled word (`native/evidence/phase-3/unlocked/`).

- **Phase 0 · 0.1 — Install full Xcode and select it** (2026-10-26). Xcode 27.1 installed and selected with `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
