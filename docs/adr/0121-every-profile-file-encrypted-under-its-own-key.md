# Every file a profile writes is encrypted under a key only Pane's processes hold

The user's requirement (2026-10-31): none of Pane's content can be seen without the PIN. ADR 0113 encrypted every profile at rest, and ADR 0116 locked Pane while open, but while Pane is unlocked the volume is mounted, and any process running as the user read the profiles' files: history, site storage, caches, preferences. ADR 0115 listed that gap. This closes it inside the engine: a process that reads files, even with Full Disk Access, finds only ciphertext, while Pane is unlocked as much as while it is locked. It reverses ADR 0113's rejection of encrypting each of Chromium's stores, which weighed a patch in every storage layer against an upgrade's cost; `docs/plans/engine-file-encryption.md` has the plan, and what each phase found.

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
- **A safety copy** of what changes, encrypted whole, comes before the first file converts, and goes after three opens in a row that came up. Two in a row that didn't restore it, once.
- **The panel** is Paper 07.5's, unchanged, past a second, for a profile a window waits for.

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
- **`files-inventory`** lists every file, for `engine/UPGRADING.md`'s step at each new Chromium.
- **`files-migration`**, from each published build it is given (0.15.2 and 0.17.2): killed three times in its first opens, everything still there, nothing plain, and the published build on the migrated data refused, the volume's files unchanged. **`files-migration-large`**: with its panel, and six kills at random points.
- **`engine-keys`**, **`memory-guarded`**, **`volume-unindexed`** (Spotlight: `.metadata_never_index` at every mount, since `mdutil` can't tell on a volume out of the Finder).
- **`pane_unittests`**, run locally since CI builds no Chromium: every I/O shape of a journal and a WAL, a mid-transaction crash rolled back from the log on disk, 200 kills in each with nothing committed lost, Raze, Recovery, VACUUM, a wrong key or none crashing and deleting nothing, LevelDB with repair, and the migration killed at any point.
- **By hand, by computer use:** `docs/plans/qa/engine-file-encryption/`, against the published 0.15.2.
