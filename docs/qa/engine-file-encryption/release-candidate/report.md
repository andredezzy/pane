# The whole checklist on the release candidate

**Build:** the candidate, stripped as a release is (`PANE_STRIP=1 package.sh`), through `tests/rigs/qa-hold.ts` on throwaway data, against Phase 0's pass on the published 0.15.2. The window taken by `screencapture -l`; recordings stayed local.

**The rules for the screen:** the foreground items ran with the Mac idle for over 10 minutes (HIDIdleTime 2,234 s when the first began), screen unlocked, guarded by a script that stops at input it didn't send or another app in front, and the app in front put back after each. One broke them, below.

| # | Item | Light | Dark | How |
|---|---|---|---|---|
| 1 | The PIN screen, unchanged from 0.15.2 | ✓ `light-01` | ✓ `dark-01` | no UI changed, so Phase 0's shots are the reference |
| 1 | A wrong PIN with real keys: "4 attempts remaining" | ✓ `light-02` | ✓ `dark-02` | `press_key`, foreground |
| 1 | The right PIN with real keys | ✓ `light-03` | ✓ `dark-03` | |
| 1 | ⌃⌘L | ✓ `light-09` | ✓ `dark-09` | `hotkey`, foreground |
| 1 | The tab that showed comes back | ✓ `light-10` | ✓ `dark-10` | |
| 1 | Lock Pane from the menu | ✓ `light-11` | ✓ `dark-11` | `invoke_menu` |
| 1 | The idle lock, a minute a second | ✓ `light-12` | ✓ `dark-12` | `PANE_TIME_SCALE`, `setLockAfter`, then the PIN: `13` |
| 1 | **The lock at the Mac's sleep** | not driven | not driven | it would sleep the user's Mac; the same lock is what ⌃⌘L runs, and `lock-while-open` covers its path |
| 1 | Time from the key to the PIN screen | | | level with 0.15.2: `docs/benchmarks/engine-encryption.md` |
| 2 | An encyclopedia, a map, a video, an offline-capable app, typed in the address field | ✓ `light-04..07` | ✓ `dark-04..07` | ⌘T, typed, Return |
| 2 | The sign-in fixture, signed in after a lock and a relaunch | ✓ | ✓ | `lock-while-open`, `files-migration` for the relaunch |
| 2 | The address bar suggesting history | n/a | n/a | Pane's field has no suggestions; history's read through `chrome.history` in `files-migration` |
| 2 | A download through the UI | ✓ `light-08` | ✓ `dark-08` | Tab and Return on the page's link |
| 2 | An extension from the Web Store, through its own buttons | ✓ `45`, `46` | | `Add to Pane` and the sheet's, by accessibility |
| 3 | A large profile from the published 0.15.2: its panel against Paper 07.5 | ✓ `migration-panel` | | 1.5 GB of blobs by hand, read in the window after (`migration-after-large-profile`); the panel on 659 MB in `files-migration-large`, whose kills and read-back are its own |
| 3 | Killed midway, then relaunched | ✓ | | six kills at random points, the stores read back: by the scenario, not by hand |
| 4 | The published 0.15.2 and 0.17.2 on migrated data: Paper 07.8's lines; no file of the volume changed | ✓ `fence-*-light` | ✓ `fence-*-dark` | `files-migration` hashes the volume's 144 files |
| 5 | The files from another app: `sqlite3`, `strings`, `file` | ✓ | | `files-from-another-app.txt`: "file is not a database", no site named, no planted value |
| 5 | **Finder's Quick Look** | skipped, the user's call | | see below |
| 6 | Speed as felt | ✓ | | recordings timed frame by frame, in the benchmark's page |

## Findings, each fixed and checked again the same way

1. **⌘T, then typing, made `https://www.google.com/<what you typed>`.** A new tab's address arrives after its field took the focus, and the selection went with it; ⌘L, with the field already focused, didn't select again. Both select now (`ToolbarView`). Not from this work: with real keys, the published 0.15.2 and 0.17.2 both gave `https://www.google.com/ab`, and the candidate `ab`. `shortcuts` catches the ⌘L half, in front (it fails on 0.17.2); only real keys in front caught the other, as the table's ⌘T steps do.
2. **The panel's fill left its track as it slid up.** The fill's animation carried its position as well as its width, so it trailed by hundreds of points. It is a shape now, whose animation is of the share alone: `migration-panel`.
3. **macOS's file event log** at the volume's root names files that change, in the clear, and an earlier Pane's logs name each site's folders. Off at every mount, and what is there goes. A test with a home whose path has no alias found it: the only way to reach the caches' real link under `~/Library/Caches`, which holds encrypted entries.
4. **Log reuse off everywhere cost a warm launch 15 ms.** It stays on where nothing is encrypted.
5. **`hosted` failed on the published builds too:** cua-driver now waits for a consent before it captures a window. `windowState` falls back on `screencapture -l`.

## What went wrong

**The Quick Look step sent keys to the user's Finder.** To see encrypted test files in Quick Look, the agent opened a throwaway folder in Finder and sent `down` and `space` by the folder window's id. They went to the user's own Finder window instead: its selection in Downloads moved, and Quick Look opened on one of the user's files, which stayed on screen behind the app in front. A capture of an unnamed Finder window then took that preview. The capture was deleted, the panel closed by accessibility with no more keys, and no more input sent to Finder; nothing of the file is in this repository. The user chose to skip Finder's Quick Look (2026-11-06): `strings`, `sqlite3`, and `file` covered what it would have shown.
