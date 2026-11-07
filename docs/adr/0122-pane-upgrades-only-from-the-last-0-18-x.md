# Pane upgrades only from the last 0.18.x

Pane opens data only as the last 0.18.x leaves it. A user on anything older installs that release's dmg first, and their data stays intact until they do. It was the user's call (2026-11-06, in the review of 0.18.0): upgrade paths for every earlier version were the largest thing in the repo, and they served almost no one. The native app began on 2026-10-29, each release has single-digit downloads with this project's own checks among them, and the repo has 2 stars. Each path was also a way for a release to lose data, which is how the review found 0.18.0's.

## What each version gets

No file carries an app version (`PersistedFile` always writes 0), so old data is known by its shape, and by what will not open.

| Coming from | What it gets | Its data |
|---|---|---|
| Electron, up to 0.6.0 | Pane starts empty | Electron's folder stays, unread, and out of a wipe's reach |
| 0.7.0 to 0.13.1 | Pane starts empty | Plain, in `com.pane.desktop`, unread |
| A PIN of 0.13.1 or before that reached this data root and was never unlocked: a bcrypt hash and no key | 07.6, which takes no keys. It counts no attempt, since the fifth erases, and makes no key | Unread |
| A plain `profiles.json` in this data root | An empty sidebar, the file kept as `profiles.json.damaged` before the first save writes over it | The file stays |
| A plain `Engine/<id>` folder | Its tab shows the sad tab: `DataVolume.link`, which deleted what stood in its place, refuses a folder | The folder stays |
| 0.14.0 to 0.17.2: a volume on the passphrase ADR 0121 moved away from | 07.8 | Every file unchanged, and the Pane that wrote it still opens it all |
| A profile whose conversion never finished: `Profile/Preferences` and no `.files-encrypted` | Its tab shows the sad tab, with `prepare` throwing before anything touches it | Unchanged, and the floor's build finishes it |
| 0.18.0 and 0.18.1 | Opens | See below |
| The last 0.18.x | Opens, and what its conversion left goes at each profile's open | |

A new profile gets its `.files-encrypted` in `prepare`, before its first file: it has nothing to convert.

**What nothing enforces.** The floor cannot be told apart between 0.18.x versions, whose data has the same shape. So 0.18.0 or 0.18.1 going straight to a later release skips 0.18.2's repair (the IndexedDB and file system stores 0.18.0 moved, ADR 0121), and those stores stay where they are: the repair read the safety copy, which goes at the next open. Anything older than 0.18.0 reaches 07.8 with its data intact, and installs the floor's dmg by hand, since the update check offers only the newest release (ADR 0051). The release's notes say so. 07.8 itself says nothing of versions: that would be new copy, which goes through Paper first.

**A conversion the floor never finished.** The floor converts every profile behind the window. A quit before it finished leaves that profile refused on the next release, intact, until the floor's build runs again.

**The other direction.** A Pane before 0.17 requires a `timezone` key no longer written, so it takes a later `profiles.json` for damaged. It opens no volume after 0.18 anyway, and it writes nothing behind 07.8.

## What went

- **The Electron migration:** `ElectronMigration`, `LaunchMigration`, `LaunchAlert`, the cookie key's adoption, `dropPendingExtensions`, `PANE_ELECTRON_DATA_DIR`, and the `electron` dev dependency, with 66 of the lockfile's 244 packages, and `@types/node`'s pin to ^22, which only it wanted.
- **Its wipe branch:** a wipe no longer reaches Electron's folder or its Keychain item. `migration.json`, which stayed so a wiped Pane never looked for them again, now goes with the wipe, since nothing reads it and it names each profile it brought.
- **The move from the former identifier,** and the cleanups of what its URL sessions and caches left.
- **bcrypt:** `CBcrypt`, `third_party/openbsd-bcrypt`, `Pin.hash`, `PinHash`, and `SecurityState.recover`. A `security.json` that does not decode opens through its key's copy beside the volume, or stays shut.
- **A plain `profiles.json`,** the proxy passwords' stores (those proxies lose their passwords), and plain profile folders, with Paper 07.5, which showed their move and the conversion.
- **What an engine before 0.15.1 kept at its root** (`stageEngineTraces`), which stays as stale clear files on an install that never ran a later one; and mount points named before 0.13.1.
- **ADR 0121's conversion, on the Swift side:** `formerVolumePassphrase`, `open(... former:)`, `FileMigrator`, and the safety copy's counting. **The engine's half follows:** `profile_migration`, SQLite's migration, and `migrateContext`, `restoreContext`, and `repairContext`.
- **Codable's tolerance** for files older than the floor, in `profiles.json` and `security.json`, and `chromiumPath`, which nothing read.

## What stays

- **The checks that stop at a plain store:** `CheckMainAtOpen`'s `CHECK` for a plain SQLite database, and `PaneCipherForRead`'s for a plain LevelDB file. Each stops the process rather than let an error reach Chromium's corruption handling, which would raze the store. `prepare` means none reaches them, and they stay as the last line behind it.
- **Patch 0010's `PaneCopyInPlainFile`:** `ObfuscatedFileUtilDiskDelegate::CopyInForeignFile` reaches it from any cross-file-system copy into a site's file system, so a file from outside goes in encrypted. It is not the conversion's.
- **Until a later floor:**
  - the sweep in `DataVolume.prepare` of `SafetyCopy`, `.safety-*`, and `.files-repaired`;
  - three keys of `profiles.json`, read when missing: `timeZone`, `savesHistory`, and `historyClearOwed`. The floor rewrites that file only when something changes, so one it opened and left is still as 0.15 wrote it. Dropping them gave an empty sidebar on the way a user comes (below).
- **The JSON envelope, `noiseSeed`, and `AppSettings`' explicit nulls,** where nil means off.

## What was rejected

- **A version in the files, checked at launch:** no published file has one, so it could serve only a later floor, and it would add what this removes.
- **07.8 naming the floor's version:** new copy, for a user the release's notes reach anyway.
- **Moving or erasing what will not open:** a refusal leaves everything as it is, so the floor's dmg can still convert it.
- **Dropping every tolerance, as first planned:** a file last saved by 0.15.2 and opened by 0.18.0 with nothing changed read on as no profiles at all.
- **Asking the update check to step through the floor:** two releases to offer is new UI, for a path that is the exception.

## How it is checked

- **`data-volume`,** with `PANE_EARLIER_BUILDS` naming published builds: 0.15.2 and 0.17.2 each write a rich session, then this Pane reaches 07.8 with every file in the volume and each of Pane's own files at the same hash and time, 193 files in all, no `.damaged` beside them, and the build that wrote them reads everything back. The same pass on the published 0.18.0 converts instead, and times out waiting for 07.8. With `PANE_FLOOR_BUILD` too, the way a user comes: 0.15.2's session, converted by 0.18.0, reads back here, and its `SafetyCopy` and markers go. It defers without them.
- **`security`:** a `security.json` holding only a bcrypt hash reaches 07.6, the right PIN and a wrong one change nothing, and no key, copy, or image appears. The published 0.18.0 opens on it and makes all three.
- **PaneKit:** a plain folder or its caches survive `prepare`, which throws; a profile with its Preferences and no marker is refused and unchanged; a new one is marked; the sweep takes what the conversion left and nothing else; a volume on another passphrase changes nothing, and that passphrase still opens it (on a real image); and what a file the floor left unsaved can miss still reads.
- **`files-encrypted`,** in CI, as ADR 0121 has it: a new profile's files, under the marker `prepare` now writes.
- **The floor's published build updating to the next for real** (the update path's check) waits for the next release.
