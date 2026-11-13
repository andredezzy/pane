# Pane installs its updates itself at quit, and where it can't, says Quit and install and quits, and the update pill's bar runs to its end, drawing back for the dismiss button

> **Partly replaced by [ADR 0114](0114-pane-identifier-is-xyz-roundedcorners-pane.md)** — the update to the new identifier does not install itself from 0.13.1 and before, whose check wants the identifier they have: their update opens the dmg.

> **On the Views line** (`docs/plans/views-migration.md`, H4): the pill (`pane/ui/sidebar/update_pill.cc`) runs its bar out to 7 points from its end and draws it back under the pointer, the dismiss button showing once the bar has cleared it (`Motion.hover`, `Motion.hoverTurn`).

> **On the Views line** (`docs/plans/views-migration.md`, H6): Settings' update row reads the one `Updater`, as the pill does: what the last check found, or the version on offer and its download (Downloading… 42% · ~2min, disabled while it runs), then Restart to update, or Quit and install, its status crossfading (`Motion.status`).

> **On the Views line** (`docs/plans/views-migration.md`, K1): the installer is `pane/updates/update_installer` (the Mac's part in `_mac.cc`; Windows' says Pane can't replace itself until W7), its steps the Swift's. The cleanup of an earlier install and the staging run on one sequence, so a launch's cleanup never removes a copy staged after it, and the shell opens the new Pane before it removes the old bundle and the dmg, 60 ms sooner. The signature is checked on the staged copy, after the copy, so the dmg's compressed image is read once: staging the Views release takes 4.4 s where checking the dmg first took 5.8 (attach 0.3 s, copy 1.9, the check 1.9, eject 0.2). Restart to update opens the new Pane as this one was opened: a scenario's copy with the mock keychain again (ADR 0115), behind the app in use when it may not take the front (`open -g`, ADR 0012); the user's Pane as before. Every published installer, 0.9.0 to 0.19.2, checks the same things: `Pane.app` at the dmg's root, the identifier, `PaneVersion`, and `codesign --verify --deep --strict`, which the Views dmg from `scripts/dist.sh` passes. `update-install` with `PANE_UPDATE_DMG` (a real dmg) shows it from the published 0.19.1 and from a Views build.

> **On the Views line** (`docs/plans/views-migration.md`, K3): PaneKit is gone with the Swift app; its install tests run as their twins in `pane_unittests` (`pane/model/updates_unittest.cc`, `docs/benchmarks/panekit-tests.md`).

> **On the Views line** (2026-11-12): Quit and install opens the dmg once Pane has exited, from a shell of its own that waits for Pane's process (`OpenUpdateFileAfterExit`, `pane/updates/update_files_mac.cc`), as a failed install at quit does. Opened through LaunchServices as Pane quit, the dmg's DiskImageMounter, launched for Pane, could outlive it by a few milliseconds, and loginwindow, finding Pane's helper still running 20 seconds on, had Background Task Management add Pane and tell the user "Background Items Added"; 0.19.2 took the same path, quitting slower, so it met the race less. `UpdateFilesTest` checks the file opens only once the process has exited.

Pane installs a downloaded update itself, at quit, so the user no longer drags it from the dmg. The user asked for it after an update from 0.7.0 ended on "Pane Not Opened". ADR 0002 had left installing to the user until a Developer ID, and DESIGN.md said Pane, ad-hoc signed, "can't safely replace itself in place". It can, through these steps:

1. **Check.** Once the dmg matches the release's SHA-256 and leaves quarantine (ADR 0002), Pane mounts it at a folder of its own: with `diskutil image attach` on macOS 27, which deprecates hdiutil's attach and detach, and with hdiutil on earlier systems. The Pane inside must carry this app's identifier (`xyz.roundedcorners.pane` since ADR 0114) at the offered version, with its signature whole (`codesign --verify --deep --strict`).
2. **Stage.** Pane copies it beside the running app, hidden (`.Pane-<version>.app`). The pill, Settings, and the lock screen then offer Restart to update.
3. **Swap.** Any quit installs the update; Restart to update quits on purpose. Pane swaps the two bundles in one rename (`renamex_np` with `RENAME_SWAP`), so the app's path never stands empty, and the running process keeps the files it has open.
4. **Finish.** A detached shell waits for Pane to exit, then removes the old bundle and the dmg. For Restart to update it also opens the new Pane, passing on Pane's own `PANE_` variables and a fixed home (`CFFIXED_USER_HOME`, with `HOME`), which only scenarios set.
5. **Clean.** A launch removes anything an earlier install left: a copy staged and never swapped in, an old bundle, and the mount point of a Pane that quit while staging.

Staging v0.8.0 took 1.5 s (mount 0.13 s, signature 0.83 s, copy 0.46 s, detach 0.07 s), during which the pill holds its full bar.

**Where Pane can't install it.** Pane installs only where it runs in place: not translocated, not from a disk image, not from a folder its user cannot write. There, and when the dmg holds a copy of another identifier or version, or a step fails, the old app stays and the dmg opens. The pill and the lock screen then say Quit and install; Settings says "Version 0.15.0 is downloaded. It installs by hand." beside an enabled Quit and install button (Paper 06.3.1; 03.3, 03.6, and 06.3 follow it). The click quits Pane, and the installer comes forward once Pane has gone (`Updater::QuitAndInstall`): the copy in Applications can only be replaced once it has quit, so the user's one step left is the drag. On the lock screen, the action that fades in under the pointer is Quit, as Restart is for a staged update. A swap macOS refuses at quit leaves the app as it was, and the dmg opens.

Before 0.15.1, Pane said "Downloaded, quit to install" there, on the pill and on a disabled Settings button, and the pill's click only opened the dmg again. The user, updating 0.13.1 to 0.15.0 across ADR 0114's new identifier (2026-10-31), read it as Pane failing to install by itself, as before, and was not told what to do.

App Management (macOS 13 and later) refuses changes to an app made by code from another team, and Pane has no team. Its swap of its own bundle went through outside `/Applications`, and in it: on 2026-10-29 the user's 0.9.0, installed in `/Applications` from its dmg, installed 0.9.1 itself through Restart to update, and reopened with nothing left beside it. A refusal would leave the app as it was and open the dmg.

**The pill's bar.** While an update downloads, the pill's progress bar runs out to 7 points from the pill's end, as the dot sits 7 from its start: the user's call, on a pill whose bar stopped 24 short, in room kept for a dismiss button that shows only under the pointer. Under the pointer the bar draws back to 32 from the end, so it sits about 14 points from the X's ink, as far as the text sits from the bar; a first try at 24 left 6.5, and the user asked for more room. The bar draws back within room it always keeps, 24 points for itself and 25 for the button, so the text never moves or cuts differently as the pointer comes and goes, and at the narrowest sidebar (200 points) the bar shrinks from 51 points to 26 rather than going.

The bar and the X take turns rather than cross (`Motion.hoverTurn`, the hover's 0.15 s after 0.08 s): the bar draws back at once, and the X fades in once the bar has cleared its place; on the way out the X goes first, and the bar runs out after it. Fading both at once left the X at 20 to 50% over the bar's tip for three frames, each way.

What was rejected:

- **Sparkle:** it needs an appcast and EdDSA signing in the release pipeline, and its installer ends in the same swap.
- **Writing into the running bundle:** a Chromium app starts its helpers from that bundle while it runs.
- **Two renames:** the moment between them leaves no app at the path, and a shutdown could make that permanent.
- **An installer package:** it asks for an admin password.
- **Waiting for a Developer ID:** users would keep dragging, and keep meeting Gatekeeper when a step goes wrong.
- **Keeping the dmg in Downloads:** 190 MB that nothing needs once it is installed.
- **Keeping the click to reopen the dmg** where Pane can't install: the user still had to find out that Pane must quit before Finder replaces it.
- **Quitting without the installer:** the dmg the download opened may have been closed by then.
- **Saying why** Pane can't (another identifier, a folder Pane can't write): the reason is one the user can't act on, and the step is the same.
- **Keeping the bar 24 short at all times:** room for a button that is not there.
- **Taking the button's room from the text under the pointer:** at narrow sidebars the text would cut differently as the pointer came and went.
- **Letting the bar shrink to nothing under the pointer:** at the narrowest sidebar the download's only sign of progress would vanish while the user looks at it.

How it is checked:

- **PaneKit's tests:** where Pane installs over itself, and what it leaves behind.
- **The `update-install` scenario:** it uses a stand-in release whose dmg holds a stand-in Pane, run on two copies of the app.
  - Offered a dmg whose Pane is another version, the pill says Quit and install, and its click quits the copy, which stays as it was and opens nothing.
  - A matching one is staged beside the app.
  - Restart to update swaps it in and opens it on the same data.
  - A plain quit swaps it in and opens nothing.
  - Neither leaves a staged or old bundle, and the installed dmg is gone.
- **The `updates` scenario:** its stand-in dmg is no disk image and holds no Pane to stage, so it says Quit and install, and its click quits Pane without downloading again.
- **Against the real release:** a build reporting 0.7.9 found v0.8.0 on GitHub 0.9 s after launch. It downloaded, verified, and staged the 190 MB dmg by 26.4 s, and quit 0.2 s after Restart to update with the bundle already 0.8.0. The new Pane opened 2.0 s later with its profile kept, nothing left beside it, and the dmg gone.
- 0.13.1 and before keep their text: they are published.
- **Paper:** 03.3 Sidebar · Footer draws the downloading pill at rest and under the pointer, and 04.27 draws it at rest, in light and dark.
- **A recording of the pill:** a build whose hover toggled itself every 0.9 s, its X drawn pure red to tell it from the bar, was recorded losslessly at 60 frames a second. It ran in light and dark, at sidebars of 260 and 200.
  - At rest the bar ends 7.0 points from the pill's end, and under the pointer 32.0. It draws back in about 130 ms, and the X starts to show once the bar has passed its place.
  - On the way back the X is at 1% when the bar crosses its place. No frame has the bar under a visible X.
  - The text's right edge stays on the same pixel in every frame.
