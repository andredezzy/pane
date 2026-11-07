# Every file a profile writes is encrypted under a key only Pane's processes hold

> **Partly replaced by [ADR 0122](0122-pane-upgrades-only-from-the-last-0-18-x.md)** — this Pane no longer converts: a volume on the former passphrase reaches 07.8, a profile with its Preferences and no marker is refused, and a new one takes its marker at `DataVolume.prepare`. The last 0.18.x is what converts. The engine's half of the conversion follows; `files-migration` and `files-migration-large` are gone.

The user's requirement (2026-10-31): none of Pane's content can be seen without the PIN. ADR 0113 encrypted every profile at rest, and ADR 0116 locked Pane while open, but while Pane is unlocked the volume is mounted, and any process running as the user read the profiles' files: history, site storage, caches, preferences. ADR 0115 listed that gap. This closes it inside the engine: a process that reads files, even with Full Disk Access, finds only ciphertext, while Pane is unlocked as much as while it is locked. It reverses ADR 0113's rejection of encrypting each of Chromium's stores, which weighed a patch in every storage layer against an upgrade's cost; the plan this work was done by, in the history before `review-and-simplify` has the plan, and what each phase found.

Without a PIN the files are just as encrypted: the data key then opens with the Keychain's secret alone, and macOS asks the user before any other process reads that item.

## The key

- **One per profile**, HKDF-SHA256 of the data key, the profile's id as salt and `Pane engine files` as info (PaneKit `DataKey.engineKey`), apart from the key that seals Pane's own files. Nothing writes it to disk, a command line, or an environment (`engine-keys` looks for it in every form, in `ps -E` and in every file).
- **It reaches the engine with the profile:** `openContext(identifier:engineKey:)` registers it before Chromium creates the profile (`pane/encryption/engine_keys`), with the roots a profile's files live under, in each form a path reaches them: the links, and the resolved volume.
- **Other processes get it in shared memory**, as os_crypt_async passes its keys, never in a mojo buffer: the network service with each `NetworkContextParams`, the Storage Service before each `Bind*StorageControl`. So each gets it back after a restart, which `engine-keys` checks by killing them.
- **A protected path with no key stops the process.** Chromium deletes or razes a store that fails to open, and a crash loses nothing. So does a key that doesn't fit, and a plain SQLite or LevelDB file in a migrated profile.
- **It stays until the lock's detach has succeeded,** profile unloads included: stores still flush as a profile is destroyed. Each process then forgets every key and says how many it has left, 0, in the engine's trace (`PaneBrowserApplication.engineKeysDropped`). At quit the keys go with the processes.

## What each store gets

| Store | Treatment | Where |
|---|---|---|
| SQLite (browser, network service) | AES-256-GCM per page, in 32 reserved bytes: a random nonce, the tag, a format and key id | a VFS, `pane/encryption/sqlite_vfs.cc` |
| LevelDB (browser, Storage Service) | AES-256-CTR, a key per file; no text log; no appends to a closed file | `third_party/leveldatabase/env_chromium.cc` |
| Whole files: Preferences, Bookmarks, TransportSecurity, CacheStorage's `index.txt`, and the rest written and read whole | AES-256-GCM | `base/files/pane_file_codec.h`, through `ImportantFileWriter`, `WriteFile`, `ReadFileToString` |
| Every Simple cache: HTTP, code, CacheStorage, service workers' scripts, shared dictionaries | Chromium's chunked AES-256-GCM-SIV, keyed entry names | `net/disk_cache/pane_cache_hook.h` |
| IndexedDB's blobs, the origin private file system | the same chunked format, since both are overwritten in place | `storage/browser/file_system/pane_encrypted_stream.cc` |
| Sync access handles | through the browser, as Incognito's: no renderer holds a descriptor or a key | `file_system_access_*` |
| Names | IndexedDB's default buckets by id (`WebStorage/<id>/IndexedDB`); CacheStorage's folders by an HMAC under the profile's key | `file_path_util.cc`, `cache_storage_manager.cc` |
| GPU caches in a profile, blob storage, the Download Service, Network Persistent State, SCT reports, No-Vary-Search's files | not on disk | one condition each |
| Tab restore, Chromium's Keychain-encrypted bookmarks and sessions | off: they name what closed, or open without the PIN | `tab_restore_service_impl.cc`, `browser_application.mm` |

`//pane/encryption` holds the formats, `core` depending on nothing but `//base` and BoringSSL so that `//sql`, LevelDB, and `//services` can use it.

## What stays plain

- **The data root:** `settings.json`, `security.json` and its copy, `migration.json`, `profiles.json` (sealed), `deletions.json`, `Pane.lock`, and the volume's image.
- **The engine's root,** which Chromium uses before the PIN: `Local State`, `Last Version`, `Variations`, `ChromeFeatureState`, its shader caches, `Safe Browsing`, the crx caches, `NativeMessagingHosts`, and the `Default` profile, holding only Chromium's defaults (ADR 0115).
- **In a profile:** `Extensions/`, installed extensions' public code; empty files and folders; and the file system's `.usage` counts, a magic and a size.

`files-encrypted` searches these too, for every value it planted.

## An earlier Pane's data

- **The fence comes first.** The first open with this Pane changes the volume's passphrase, before any file converts, so 0.15 to 0.17, whose engine would raze stores it can't read, say "Pane can't open your encrypted data" (Paper 07.8) and open nothing. A change is one call to APFS, so a kill leaves one passphrase or the other, and this Pane tries both.
- **Then each profile converts, in place,** the one a window waits for at once, the others behind it. The HTTP and code caches go and rebuild. Each file goes to a temporary one, is verified by decrypting it, and is renamed over the original: SQLite through `VACUUM INTO`, the rest by its own format, Simple cache entries with their names rekeyed. A kill at any point loses nothing; the next launch goes on. An empty file beside the profile, in the volume, says it is done.
- **A safety copy** of what changes, encrypted whole, comes before the first file converts. Two opens in a row that a kill ended, before any came up, restore it, once: never after one came up, which would take back what was written since, and a quit or a lock is no failure. 0.18.0 counted anything under 5 s as a failure, even past a good open, and rolled healthy profiles back unasked. A restore and a migration never share a folder, a lock's detach waits for both, and a restore that fails leaves the profile refused, tried once. From its third good open it goes, once 0.18.2's repair has read its listing.
- **The panel** is Paper 07.5's, unchanged, past a second, for a profile a window waits for.

## What 0.18.0's migration got wrong, and what 0.18.2 puts right

- **A first party's IndexedDB could move into a third party's bucket.** The migration took every default bucket by the identifier of its origin, and a partitioned storage key parses to the same origin as the first party's, so the last row in key order, a partitioned one if any, took the store. The site then read an empty store, and an embed of it read the site's. A bucket that already held a store got it nested inside. The migration now reads first parties only. **The repair** runs once per profile, before it next opens (`.files-repaired`): the safety copy's listing names the first parties that had a store, and among the partitioned buckets whose store is older than the copy, the last in key order is where it went. It moves back, nested or not. A store the first party has again keeps its place and the other stays too: nothing proves which it wants. **Where the safety copy is gone, the store stays where it is** (the user's call, 2026-11-06): nothing else proves which store moved, and a guess could hide a real one. The release's notes say so. An in-app notice would be new UI.
- **Partitioned file systems were sealed as whole files**, since only `File System` was taken for a stream, where partitioned storage writes `FileSystem`. They read as empty, with the bytes intact: the repair puts each back as a stream, its magic proving what it is.
- **Background Fetch's CacheStorage folder collided with the Cache API's**, nesting one in the other: the hash the folder is named by covers its owner's suffix, which only that hash tells.
- **One malformed store barred its whole profile**, at every launch: VACUUM reads what Chromium never touched. A store SQLite calls `CORRUPT` or `NOTADB` now goes as Chromium's raze would have taken it, with the safety copy keeping it. A bad cache entry goes too. `quick_check`, run with no callback, had been saying yes to anything; it now has to say `ok`, and a copy that fails it while its original passes is this code's failure. A disk too full for the copy still bars the profile: a message for it is new UI.
- **A file over 1 GiB in a whole file's place stopped the process at every launch** (`base::File` reads 2 GiB at most): it stays as it is, in the encrypted volume.

## What can still take a store

- **The quota database resets only when SQLite calls it malformed** (patch 0010, `quota_database.cc`): Chromium resets on any failed open, and that dooms `WebStorage`, where Pane keeps every default bucket's IndexedDB, where Chrome keeps first parties' outside it. Anything else stops the process, which loses nothing. A really malformed quota database still takes IndexedDB with it.
- **A page 1 a crash tore, with a journal to restore it, opens.** SQLite reads the header as it opens, before any playback, and that read failed the open, which reached that reset. It now comes back short, which SQLite takes for an empty header.
- **Past a playback, a WAL's page that won't authenticate is an error.** Zeros read as a malformed database, which Chromium razes.
- **Chromium's cookie key stays the one it was.** 158 made "v10" unavailable on a data directory other than Chrome's default, which sealed every cookie out of reach of the release before it (`KeychainKeyProviderUserDataDirProtection`). It guards one app's data from another copy of Chrome, which Pane's own key does already, so it's off. `data-volume`, carrying a session across the floor, is what caught it.
- **A kill between an append's writes leaves a stream's tail readable:** its last chunk may be marked as not the last, and a read tries both marks (`encrypted_cache_file.cc`, one more Chromium file, since a repair at open would race a writer). The mark guards only against someone who truncates the file, whom the threat model leaves out. A power cut can still tear a chunk.

## What may stay plain

`files-encrypted` still searches these for what it planted. Any addition needs its reason here.

- **The data root:** Pane's own files: `settings.json`, readable at the user's call; `security.json` and `Data.security.json`, which hold the PIN's length and the sealed key; `profiles.json`, sealed; `deletions.json`, which names only ids; `Pane.lock`. The volume's image, `Data.sparsebundle`.
- **macOS's in the volume:** `.fseventsd/no_log` and `fseventsd-uuid`. Its log, which names files that change, is off at every mount.
- **The engine's root** (`Engine/`), which Chromium uses before the PIN: `Local State`, `Last Version`, `Variations`, `ChromeFeatureState`; the GPU's shader caches, `GraphiteDawnCache` and `GPUPersistentCache` (ADR 0115); `Safe Browsing`, `component_crx_cache`, `extensions_crx_cache`, `Webstore Downloads`, `NativeMessagingHosts`; the `Default` profile, holding only Chromium's defaults (ADR 0115), and its caches under `~/Library/Caches`.
- **In a profile:** `Extensions/`, installed extensions' public code; empty files and folders; and the file system's `.usage` counts, a magic and a size: metadata, as a file's size is.

## Residuals

- **A process that writes.** It can change Pane's bundle, which ad-hoc signing does not guard, and steal the key at the next launch; or edit Local State. This waits for a Developer ID (ADR 0048). Nothing here depends on a feature flag or a field trial.
- **Pane's processes' memory, against Apple's inspection tools.** `lldb` is refused, and a program of the user's own gets no task port, on the browser, the network service, and the Storage Service. But `vmmap`, `heap`, `leaks`, `stringdups`, and `sample` run, since Apple's tools carry `com.apple.system-task-ports.read`: they print regions, class names, and stacks, and a value planted in the process is in none of it. The plan had required all of them to fail; measuring showed they don't, on any app, and the user chose on 2026-11-05 to go on with this recorded. `memory-guarded` checks exactly this.
- **Root and the kernel; the screen and Accessibility; Apple Events,** which TCC asks the user for.
- **What the user takes out of Pane:** downloads, print jobs, drags and the clipboard, site notifications.
- **Metadata:** file sizes and times, and the names of installed extensions' folders. Hiding those means encrypting `Extensions/`, a week more, which the plan left to the user.
- **An attach writes.** Starting an earlier Pane on migrated data changes the image's own bands (APFS checkpoints) and its `Engine/` root, as any launch does: no file inside the volume, and not `security.json`, its copy, or `profiles.json`, whose hashes `files-migration` compares.

## What was rejected

- **Interposing file calls with dyld:** whether the hardened runtime honours it is unknown, libSystem's own calls escape it, and it has to emulate POSIX exactly.
- **A Group Container alone** (after a Developer ID): Full Disk Access still reads it.
- **Everything in memory, as Incognito:** the data would be gone at every quit.
- **A SQLCipher fork:** SQLite removed codec support, and a VFS survives upgrades. **XTS for SQLite:** no integrity, and deterministic.
- **IndexedDB's SQLite backend:** it deletes LevelDB data, and has not launched.
- **Dropping keys as each profile closes:** stores still flush as their profile is destroyed.
- **CTR for blobs and the origin private file system,** as the plan first had: an overwrite reuses its keystream.
- **Binding a stream file to its folder,** as the plan first had: it guards only against a process that writes, which is out of scope, and a rename across folders would have stopped a store.
- **Caching page 1's plaintext per file,** as the plan first had: a whole-page read and decrypt costs microseconds, and a stale copy would hide another connection's write.
- **The code cache and shared dictionaries off,** as the plan first had: through the same hook they read as ciphertext, and repeat visits keep them.

## How it is checked

- **`files-encrypted`** (in CI): a rich session, with Pane open and unlocked, then every file under the profile, its caches, the data root, the home, and `$TMPDIR`: each in a format of Pane's or on the allow-list, none holding a planted value, no name giving a site away, in clear or as an unkeyed hash. The session's values read back through it all.
- **`files-encrypted`** also lists every file (`inventory.json`), for `engine/UPGRADING.md`'s step at each new Chromium.
- **`files-migration`**, from each published build it is given (0.15.2 and 0.17.2): killed three times in its first opens, everything still there, nothing plain, and the published build on the migrated data refused, the volume's files unchanged. **`files-migration-large`**: with its panel, and six kills at random points.
- **`engine-keys`**, **`memory-guarded`**, **`volume-unindexed`** (Spotlight: `.metadata_never_index` at every mount, since `mdutil` can't tell on a volume out of the Finder).
- **`pane_unittests`**, run locally since CI builds no Chromium: every I/O shape of a journal and a WAL, a mid-transaction crash rolled back from the log on disk, 200 kills in each with nothing committed lost, Raze, Recovery, VACUUM, a wrong key or none crashing and deleting nothing, LevelDB with repair, and the migration killed at any point.
- **By hand, by computer use:** `docs/qa/engine-file-encryption/`, with their screenshots kept locally,, against the published 0.15.2.
- **A real update:** the published 0.15.2 took 0.18.0 from GitHub, swapped itself at quit, and the data it had written migrated and read back.
- **What it costs:** `docs/benchmarks/engine-encryption.md`. All budgets met but the first launch on new data, +12 ms over 0.15.2 where the budget is +10, which the user accepted (2026-11-06).
