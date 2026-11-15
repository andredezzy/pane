# On Windows Pane installs and updates itself: one folder in %LOCALAPPDATA%\Programs\Pane\Application, staged beside it and swapped whole once Pane has exited, with no launcher at launch

Pane on Windows is installed and updated by Pane's own code, as on the Mac (ADR 0092), not by Chromium's installer (W7, decided 2026-11-15 from the research in `evidence/views-migration/runs/p5-w7-research-notes.md`). The app is one folder, `%LOCALAPPDATA%\Programs\Pane\Application`, laid out flat, apart from the data in `%LOCALAPPDATA%\Pane` (ADR 0121), so the panic wipe and the disk audits never meet it. Shortcuts and Pane's ProgIDs start its `chrome.exe`, which loads the `chrome.dll` beside it where no version folder is (`chrome/app/main_dll_loader_win.cc`): nothing more runs at launch. An update stages the new version beside the folder and a small helper swaps the two once Pane has exited, through Chromium's `WorkItemList`, which rolls back; the install window and the uninstall dialog are Pane's own, drawn in Paper (11.12.1 to 11.12.18). It takes no patched file but chrome.exe's icon (ADR 0004).

## Why

- **Chromium's installer needs a version of its own for every release.** `setup.exe` installs the highest version folder and only repairs a running install of the same version, so each Pane release would need its own 4-part Chromium version, a patched `chrome/VERSION`, which the Mac's engine shares: its framework's version folder, `publish-engine.sh`'s check, a new engine key and both engines rebuilt at every release, and a made-up version shown to Google's update servers, DevTools, and Installed apps. With `chromium_strings.grd`, `chrome_exe.rc`, and `setup_main.cc` (its Crashpad and histograms, ADR 0115), at least 4 more patched files where Windows had 2 left.
- **Its traps:** App Paths `chrome.exe`, shared with an installed Chrome; Chrome's 6-hour restart of a Pane with no Browser open, a locked one; the app inside the data root; an uninstall that deletes `User Data` alone; no window while installing.
- **Pane's own keeps the Mac's design** (stage beside, swap after exit, a detached helper finishes, a launch cleans up), packages each release from the engine archives as CI does on the Mac (ADR 0049), shows the web Chromium's real version, and costs nothing at launch.

## The user's calls (2026-11-15, Paper 11.12)

- **A,** the install also makes a desktop shortcut, beside Start's (11.12.3's B).
- **B,** uninstall keeps the data by default, deleting it offered, the panic wipe's way (11.12.9's A).
- **C,** chrome.exe carries Pane's icon, by a hunk in patch 0001's `chrome_exe.rc` (11.12.15's A; ADR 0004's count to 96, 6 of Windows' 7).
- **D,** the shortcuts, pins, and the desktop's follow the App icon setting, Pane rewriting them at a change (11.12.16's A).
- **E,** a swap that failed at restart says so in one quiet line above the pill, for that launch only (11.12.14's A).
- **F,** a page and a PDF take Pane's own document icons (11.12.2's A), placed by its placement 1: the window centred in the page under its fold, the PDF's raised over a tag at the page's foot, a bar with no letters below 48.
- The rest approved as drawn: Pane.ico (11.12.1), Installed apps (11.12.4), Default apps (11.12.5), the install window and its states (11.12.6 to 11.12.8), the uninstall dialog's states (11.12.10), and the update's words on Windows (11.12.11 to 11.12.13), the shortcut's tooltip "Every profile, its own internet." with them.

## The package (W7's first part, 2026-11-15)

- **What the folder holds** is `packaging/windows/pane.release`: the engine's files Chromium's installer packs (`chrome.release`'s `[GENERAL]`, `[HIDPI]`, `[SNAPSHOTBLOB]`, and `[DXC]`) that Pane's build has, without Chromium's Start tiles' logos; Pane's skies and icon files beside chrome.dll (ADR 0089); `pane-version`, Pane's version (ADR 0044); and Chrome's CRLSet (ADR 0040). `scripts/package-win.sh` lays it out from `out/Win` or an engine archive (`scripts/windows-engine-files.sh` lists the engine's part), refuses a package without a CRLSet, as `dist.sh` does, and compresses the folder's contents as Chromium's installer archive is (7-Zip's LZMA with BCJ2) into the payload Pane's installer will carry, `packaging/release/Pane-<version>-x64-app.7z`: 278 files, 472 MB, 130 MB compressed.
- **Each system takes its own file** from a release that carries both: the Mac's `-arm64.dmg`, Windows' `-x64.exe` (ADR 0002).
- **The engine archives** gain a platform column, Windows' archive the engine's files the folder holds (ADR 0049).
- **Nothing writes into the folder but an install and an update:** what Pane's launch logs before Chrome sets its logging up goes to the system's log and stderr, the Mac's default, where Windows' own default was a `debug.log` beside chrome.exe (`pane_main_delegate.cc`); Hunspell's dictionaries are kept in the data (ADR 0040).

## Still to come

- **W7b,** Pane's installer, `Pane-<version>-x64.exe`: the install window, the payload extracted with Chromium's `installer::UnPackArchive`, the shortcuts through ShellUtil with Pane's words (`installer::SetTranslationDelegate`), Default apps' entries without App Paths, the ProgIDs naming `PaneHTM.ico` and `PanePDF.ico`, the uninstall entry, the toast activator, and the uninstall dialog.
- **W7c,** the update: staging into `Update\<version>`, the helper's swap, Restart to update and a plain Exit, E's line, the Windows engine archive published, and the release workflow publishing both files.

## What was rejected

- **Chromium's `mini_installer` and `setup.exe`** (above).
- **A stub launcher over `app-<version>` folders** (Squirrel's shape): one more process at every launch. It stays the fallback, measured first, if the swap after exit fails in the VM.
- **MSIX:** it needs a trusted signature, so it can't carry an unsigned alpha (U6).
- **The app inside the data root,** `%LOCALAPPDATA%\Pane`: the panic wipe and the audits would meet it.

## How it is checked

- **The first part** (`evidence/views-migration/runs/p5-w7a-notes.md`): `pane_unittests` (each system's file, Windows' version file, the CRLSet's place); in the test VM, the payload laid out in `%LOCALAPPDATA%\Programs\Pane\Application` as an install will, and from there About Pane reading `pane-version`, the update check comparing against it, `crlset` passing, the shared suite passing whole behind (75 of 75), and Pane launched as its user does (`tests/vm/user-launch.ts`), the folder holding the package's files alone afterwards; chrome.exe's icon in Task Manager, its properties, and Explorer, Task Manager showing Chromium's logo for an engine without the patch, and the icon files at their sizes in Explorer, light and dark, against their boards.
- W7b and W7c add theirs: the first install through the shell, the uninstall, and the update's swap in the VM (`p5-w7-research-notes.md`, section 9).
