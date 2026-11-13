# Phase 0: the QA rig's first pass, on the published 0.15.2

**Build:** `Pane-0.15.2-arm64.dmg` from the v0.15.2 release (SHA-256 `87014e17…`, matching GitHub's digest), copied to a throwaway folder, run through `tests/rigs/qa-hold.ts` on throwaway data. Nothing of the user's was touched.

**The rig:** `qa-hold` holds a test copy open; `tests/rigs/qa-send.ts` sends it setup commands through the automation channel; cua-driver presses its controls by accessibility, in the background; `screencapture -l` takes the window without the front.

| # | Item | Result on 0.15.2 | How |
|---|---|---|---|
| 1 | PIN screen, a wrong PIN, the right PIN | Dots and numpad as Paper 07.1; "4 attempts remaining" after 9999; 1234 opens the window | Pressed by accessibility, in the background: `01-pin-screen.png`, `02-wrong-pin.png`, `03-unlocked.png` |
| 1 | The PIN with real keys, ⌃⌘L, Lock Pane from the menu, the idle lock | **Owed: needs the front.** The Mac was in use (idle under a minute). `invoke_menu` refuses in the background: macOS disables the app's menu while it is behind | Foreground pass, when the Mac is idle 10 min or the user allows it |
| 2 | A real site | Wikipedia loads and renders in a new profile | `10-wikipedia.png` |
| 2 | Sign-in, address bar suggestions, download, Web Store extension through the UI | Owed with the foreground pass | |
| 5 | The files from another app | **Everything readable:** `sqlite3` lists History's URLs; `strings` finds each site's cookie names, Local Storage's origin, and the HTTP cache's keys with whole URLs | `files-from-another-app.txt` |
| 6 | Speed | Benchmarks in `docs/benchmarks/engine-encryption.md`; frame-by-frame recordings owed with the foreground pass | |

Items 3 and 4 (migration, the downgrade fence) have nothing to compare on 0.15.2.

`inventory-0.15.2.json` is `files-inventory`'s list on 0.15.2: 357 files, with each one's kind, writer, and what reads as plain. No `Sessions/Session_*` appears, and nothing new in `$TMPDIR`.
