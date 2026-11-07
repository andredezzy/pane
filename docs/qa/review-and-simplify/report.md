# The checklist on 0.19.0's candidate

**Build:** the candidate from its dmg, stripped as a release is, on Chromium 158.0.8127.18, through `tests/rigs/qa-hold.ts` on throwaway data, against the published 0.18.0 where the item compares. Captures are of the test copy's own window, and stay out of the repository.

**The rules for the screen** (`docs/building.md`): each pass began with the Mac idle over 10 minutes and its screen unlocked, the user told first, with the frontmost app's pid proved to be the test copy's before each key and click, and any input the rig did not send stopping the pass. The app that was in front was put back. Nothing else got a key: cua-driver itself refused a click whose point fell over another app, at a resized window's corner.

| # | Item | Light | Dark | How |
|---|---|---|---|---|
| 1 | The PIN with real keys, and a wrong one: "4 attempts remaining" | ✓ | ✓ | `press_key` |
| 1 | Five wrong PINs in Settings lock Pane and drop the key (D1) | ✓ | ✓ | `press_key`; the PIN opens it again |
| 1 | ⌃⌘L, then the tab that showed comes back | ✓ | ✓ | `hotkey` |
| 1 | Lock Pane from the menu | ✓ | ✓ | `invoke_menu` |
| 1 | The idle lock, a minute a second | ✓ | ✓ | `PANE_TIME_SCALE`, `setLockAfter` |
| 1 | The PIN screen against Paper 07.1 and 07.8 | ✓ | ✓ | by eye, and identical by pixel to 0.18.0's |
| 2 | An encyclopedia and a shop, typed in the address field | ✓ | ✓ | ⌘T, typed, Return |
| 2 | The sign-in fixture after a lock and a relaunch | ✓ | ✓ | the tab here, `lock-while-open` and `data-volume` for the relaunch |
| 2 | Address suggestions | ✓ | ✓ | ⌘L, typed |
| 2 | A download through the page | ✓ | ✓ | Tab, Return: `state.downloads` says complete |
| 2 | An extension from the Web Store, by its buttons | ✓ Dark Reader, by pointer | ✓ uBlock Origin Lite, Return in the sheet | "Add to Pane", the sheet, the toolbar's icon, its popup, the extensions menu |
| 3 | What changed, by pixel with 0.18.0 | ✓ | ✓ | below |
| 4 | An earlier Pane's data opens, and everything reads back | ✓ | n/a | `files-migration` from 0.14.2, 0.15.2, 0.16.3, and 0.17.2, the repair from 0.18.0 and 0.18.1, and 0.18.2's data, read back: below |
| 5 | The files from another app | ✓ | n/a | below |
| 6 | Speed as felt | ✓ | n/a | below |
| 7 | The profile sheet's group field and swatches | ✓ | ✓ | by pointer; the swatch's ring follows, the color saved |
| 7 | ⌃⌘F full screen, in and out | ✓ | ✓ | `hotkey` |
| 7 | The Edit menu's Select All | ✓ | n/a | `invoke_menu` |
| 7 | **The Edit menu's Cut, Copy, and Paste** | not driven | not driven | they use the clipboard, which holds the user's: `docs/human-checklist.md` |

Also by pointer: a window's live resize from its corner, 1512 × 883 to 1251 × 711, sidebar and page following (E7). Its edge can't be dragged out past the window from a cua-driver desktop point: the point then lies over another app, and cua-driver refuses it.

## What changed, by pixel

74 renders on 0.18.0 and the candidate, the same tests, the same session: the lock's PIN, wrong-PIN, Change, and empty screens in both themes, and those of `extension-details`, `extensions-menu`, `extension-shortcuts`, `http-auth`, `profile-sheet`, `updates`, and `data-volume`. 54 are identical, the lock's eight and 07.8's two among them. The rest:

- **Two differ by 70%:** `menu-light`, `more-menu`. The page behind the menu is dark or white by when Dark Reader, just installed, reaches a tab already open, and 0.18.0 shows both (white once, dark once, in two more runs). The menus are identical.
- **Eighteen differ by under 0.04%, in text alone:** what changes from run to run (a fixture's port number, the version in Settings), and the name typed in the sheet, a device pixel to the left while its field has the focus. `Fields.swift` hasn't changed since 0.18.0; what has is when a field takes the focus (`e9d0b74`).

## Earlier Panes' data

The candidate is what updates each of them (ADR 0122), so each wrote its own data and the candidate opened it: from 0.14.2, 0.15.2, 0.16.3, and 0.17.2, converted through kills at 150, 400, and 800 ms into the first open, with every store read back, cookies included, and nothing plain on disk. What 0.18.0 and 0.18.1 left wrong is put right. 0.18.2's data reads back. `files-migration-large` converts 600 MB, and its render shows 07.5 on the candidate.

**Not by hand:** 07.5 with a real earlier profile, typed through with the PIN, and an earlier build updating to the candidate end to end. The conversion and the swap are each checked, by `files-migration` and `update-install`, and meet in the real update from published builds after the release.

## The files from another app

With Pane open and unlocked, on a rich session's profile: 142 files by `strings`, none naming the site, none holding a planted value. `sqlite3` reads no table of `History`, `Cookies`, `Web Data`, or `QuotaManager`, and `file` calls each `data`. The control is `files-encrypted`'s planted values, found in the plain copy.

## Speed as felt

The same steps on the same setup, one profile and one tab on a real page, recorded with `screencapture -v` and timed frame by frame, each three times. In milliseconds into the recording, the key about a second in:

| Steps | What | 0.18.0 | Candidate |
|---|---|---:|---:|
| ⌃⌘L | The lock in place | 1600 to 1617 | 1600 to 1683 |
| The PIN's four keys | The page back | 3217 to 3500 | 3367 to 3383 |
| ⌘R on a real page | First paint | 1017 | 1017 |
| ⌘R on a real page | Settled | about 1250 | about 1250 |
| ⌘T | The new tab's first frame | 1000 to 1017 | 967 to 1017 |

Past that, a reload shows only a change at the threshold once a second, on both.

## Findings

- **The profile sheet's Group is stored and shown nowhere.** `profile.group` saves, and neither the sidebar nor anything else reads it, in 0.18.0 too, and since at least 0.14. Showing or removing it changes what Pane shows, so it waits for the user.
- **The window's frame is in the preferences the user's Pane reads.** A test copy has the same bundle identifier, and cfprefsd ignores a home of its own, so a pass that resizes a window moves where the user's next opens. Noted in `tests/lib/browser.ts`; this pass put the frame back.
