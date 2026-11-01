# Plan: every profile file unreadable to other processes while Pane is unlocked

Status: proposed, 2026-10-31. Nothing here is built yet. When it is, the decision goes into ADR 0119 (one record per subject, AGENTS.md), and this plan's log (at the end) records what each phase found.

## The goal

The user's requirement (2026-10-31): none of Pane's content can be seen without the PIN. ADR 0113 encrypts every profile at rest, in an APFS volume the PIN unlocks, and ADR 0116 locks Pane while open. But while Pane is unlocked the volume is mounted, and any process running as the user reads the profiles' files: history, site storage, caches, preferences. Only cookies and saved passwords are encrypted again, by Chromium, under the Keychain's "Pane Safe Storage" key (ADR 0115 lists this gap).

This plan closes it inside the engine. Every file a profile writes is either encrypted with a per-profile key that only Pane's processes hold in memory, or kept in memory. Another process, even one with Full Disk Access, then reads only ciphertext, while Pane is unlocked as much as while it is locked. Pane's processes run under the hardened runtime (ADR 0115), so their memory, and the key in it, is out of other processes' reach.

### Done when

On a release build, all of these hold:

1. **No plaintext while open.** The `files-encrypted` scenario passes: after a rich session (sign-ins and cookies, history, Local and Session Storage, IndexedDB with blobs, CacheStorage, a service worker, the origin private file system with a sync access handle, a download, an extension), with Pane open and unlocked, every file under each profile's folder, its cache folder, and the data root is ciphertext or on this plan's allow-list. No visited URL, cookie value, stored value, or page text appears in any of them.
2. **Nothing lost.** A profile written by 0.15.2 migrates on its first open: history, cookies (signed in), saved passwords, site data, extensions, and settings all survive, and the migration survives being killed at any point.
3. **Lifecycle intact.** Lock, unlock, quit, a crash, profile deletion, and the panic wipe work as before, and no key stays in any process once Pane locks.
4. **Green.** The full scenario suite, PaneKit's tests, the engine's new unit tests, lint, typecheck, and knip pass.
5. **Fast.** The benchmarks stay within the budgets below, against 0.15.2's.
6. **Recorded.** ADR 0119 records the decision; ADRs 0113 and 0115 point to it.

## Threat model

**In scope:** a process running as the user (an app, a script, malware, a backup or indexing tool, with or without Full Disk Access) that reads files under the data root, the profiles' cache folders, or `$TMPDIR` while Pane is unlocked.

**Out of scope, recorded as residuals in ADR 0119:**

- **A process that writes.** It can change Pane's bundle, which ad-hoc signing does not guard (no library validation, no `restrict`), and steal the key at the next launch. It can also edit Local State or swap files around. This waits for a Developer ID (ADR 0048), with library validation and App Management. Two cheap measures still go in (Phase 0): Pane ignores Local State's chrome://flags entries, and no encryption depends on a feature flag.
- **Root and the kernel.**
- **The screen and the UI:** Screen Recording and Accessibility, which TCC asks the user for.
- **Apple Events:** Automation, which TCC asks the user for.
- **Pane's processes' memory:** the hardened runtime guards it.
- **Metadata:** file names, sizes, and times. Two leaks are reduced: IndexedDB's folders stop naming sites (Phase 2), and the HTTP cache's file names become keyed hashes (Phase 3). CacheStorage's folder names stay unkeyed origin hashes unless Phase 3 keys them.
- **The engine's Default profile:** Chromium opens it before the PIN. The audit of 0.15.1 found only Chromium's defaults in it (ADR 0115).
- **Installed extensions' code** (`Extensions/`): it is public Web Store code. Their settings, rules, and storage are in scope.

## Design

### The key

- **Derivation.** The app derives a per-profile files key, HKDF-SHA256 of the data key the PIN releases, with the profile's id as salt and `pane-files-v1` as info, in the pattern of `app/PaneKit/Sources/PaneKit/DataKey.swift`. Nothing writes it to disk, puts it on a command line, or in an environment.
- **Handover.** `openContext` takes the key (a new parameter on `PaneBrowserApplication`). The engine registers it in a per-process registry before `ProfileManager::CreateProfileAsync` (`engine/pane/browser/browser_application.mm:351-353, 380-403`). It registers both the link path (`Engine/<id>`) and the resolved one (`Engine/Volume/<id>/Profile`), as well as the profile's cache folder: SQLite sees resolved paths, and other code sees links.
- **Other processes.** The key reaches the network service and the Storage Service as os_crypt_async does, in a shared memory region, never in a mojo buffer (`components/os_crypt/async/common/encryptor.mojom:13-32`). The vehicle is a field in each process's parameters: `NetworkContextParams` for the network service, and the Storage Service's mojom.
- **Fail closed.** A protected path with no key crashes the process. It never returns an error: Chromium deletes or razes stores that fail to open (`sql/error_delegate_util.cc:59-98`, IndexedDB's `bucket_context.cc:1106-1122`, Local Storage's `local_storage_impl.cc:565-609`, leveldb_proto's `leveldb_database.cc:87-93`).
- **Lock and quit.** `closeContext` (ADR 0116) unregisters the key in every process, once the profile is destroyed and its network context and storage partition are gone.

### What each kind of file gets

| Store | Process | Treatment | Hook |
|---|---|---|---|
| SQLite: History, Favicons, Top Sites, Shortcuts, Web Data, Account Web Data, Login Data (+ For Account), Network Action Predictor, Affiliation Database, QuotaManager, DIPS, CdmStorage.db, heavy_ad_intervention_opt_out.db, declarative_performance_observer.db, MediaDeviceSalts, ServerCertificate, PrivateVerificationTokens, Extension Cookies | browser | encrypted | Pane VFS (Phase 1) |
| SQLite: `Network/` Cookies, Reporting and NEL, Trust Tokens, Device Bound Sessions; `Shared Dictionary/db` | network service | encrypted (Shared Dictionary: off) | Pane VFS in the network service |
| LevelDB: IndexedDB, Service Worker database, Platform Notifications, shared_proto_db, GCM Store, Sync Data, Site Characteristics Database, extension state, rules, scripts and settings, File System metadata | browser | encrypted | ChromiumEnv hooks (Phase 2) |
| LevelDB: Local Storage, Session Storage | Storage Service | encrypted | ChromiumEnv hooks, key over mojo |
| IndexedDB blob files | browser (blob subsystem) | encrypted | `write_blob_to_file.cc`, `file_stream_reader_to_data_pipe.cc` |
| HTTP cache | network service | encrypted | Chromium's encrypted cache, per-profile provider (Phase 3) |
| CacheStorage and its `index.txt`; Service Worker ScriptCache | browser | encrypted | disk_cache encryption delegate, generalised; whole-file codec |
| Code cache (js, wasm) | browser | off, then encrypted if repeat visits suffer | `GetGeneratedCodeCacheSettings` |
| GPU caches in a profile (GPUCache, DawnWebGPUCache, DawnGraphiteCache) | browser | off | patch at `render_process_host_impl.cc:2013` |
| Preferences, Secure Preferences, AccountPreferences, Bookmarks (+ `.bak`), TransportSecurity, Web Applications, Custom Dictionary.txt, DNR dynamic rules | browser, network | encrypted | whole-file codec (Phase 4) |
| Network Persistent State, SCT Auditing Pending Reports | network service | memory-only | clear their names in Pane's `ConfigureNetworkContextParams` |
| Tab restore (`Sessions/Tabs_*`), `Sessions_Encrypted`, `EncryptedBookmarks2` | browser | off | drop `CLOSE_CREATE_HISTORICAL_TAB`; tab restore never persists; the Keychain-encrypted copies off |
| File System / origin private file system | browser; renderers with sync access handles | encrypted | stream reader and writer, disk delegate; sync handles through the mojo delegate |
| blob_storage, Download Service | browser | memory-only | one-condition patches |
| Visited Links | none in 157 (partitioned, built from History) | stale file deleted | migration |
| Extensions' code | browser, unzip utility | out of scope | |

### Formats

All of them use BoringSSL, already in Chromium; AES runs in hardware on Apple silicon.

- **SQLite.** 32 reserved bytes per page: a 12-byte random nonce, a 16-byte AES-256-GCM tag, and 4 bytes of format and key id.
  - **Authenticated data:** the kind of file and the page number (main file) or offset (journal, WAL), never the path, since Chromium copies database files.
  - **Page 1:** bytes 0-15 carry a Pane magic in place of "SQLite format 3\0" (restored on read), and bytes 16-23 stay clear and authenticated. SQLite's own magic marks a legacy plaintext file; an empty file is a new encrypted one.
- **LevelDB and stream files** (IndexedDB blobs, the origin private file system).
  - **Header:** 32 bytes: magic, version, flags, a 16-byte random file id, and an 8-byte key check.
  - **Per-file key:** HKDF-SHA256 of the files key, with the file id as salt and, as info, the store's directory relative to the profile, normalised for `__tmp_for_rebuild` and `lost/`.
  - **Encryption:** AES-256-CTR, the counter being the logical block index. LevelDB never rewrites a byte in place once appends are refused (below). A file fails for good after any failed write, so a keystream is never reused.
- **Whole files.** A magic and a version, then a random nonce, then AES-256-GCM over the whole content. These files are small and rewritten whole (ImportantFileWriter writes a temp file and renames it).
- **Caches.** Chromium's own chunked format: AES-256-GCM-SIV in 4 KiB chunks with a per-file HKDF key (`services/network/enterprise/encryption/chunked_encryptor.h:20-50`), and keyed HMAC file names.

## Phases

Each phase ends green (its checks, and the scenarios it touches) and is committed. Estimates are for one agent working full time, measured where they can be.

### Phase 0: groundwork (about 1 week)

1. **Baseline.** Measure 0.15.2's build with the existing benchmarks, and record the numbers in `docs/benchmarks/engine-encryption.md`:
   - `benchmark-pages`: warm loads, and repeat visits after a relaunch.
   - `benchmark-launch`, `benchmark-unlock`, `benchmark-memory`.
   - New micro-benchmarks: a History and omnibox query, the cookie load at startup, IndexedDB and Local Storage reads and writes.
2. **Inventory.** A `files-inventory` scenario runs the rich session and lists every file under the profile, its cache folder, the data root, and `$TMPDIR`. For each it notes the kind, the writer, and whether its content is plain. This list becomes the coverage checklist, and it settles the open question of `Sessions/Session_*`.
3. **Audit scenario.** `files-encrypted` runs the same session, then scans every file while Pane is open:
   - It looks for markers: SQLite and LevelDB headers, JSON, HTML, and URLs.
   - It checks entropy (at least 7.9 bits a byte for files over 4 KiB).
   - It searches for the exact strings the session planted: a URL, a cookie value, stored values.

   It starts in report mode, which lists today's plaintext, and turns strict once Phase 4 ends, with the allow-list in the scenario and the reason for each entry.
4. **Spotlight.** With Pane open, check whether Spotlight indexes the mounted volume (`mdutil -s` on the mount point). If it does, or if that can't be told, write a `.metadata_never_index` at the volume's root on every mount (`DataVolume.swift`), and confirm.
5. **Hardening.** Pane ignores Local State's chrome://flags entries in hosted mode (`chrome/browser/metrics/chrome_feature_list_creator.cc:199-221`), so a process that writes Local State cannot flip features. No encryption depends on a feature or field trial: each is on at compile time.
6. **Key spike.** Build the key's path end to end with an encryptor that changes nothing:
   - the app's derivation;
   - `openContext` with the key;
   - the registry, by link and resolved path;
   - the shared-memory handover to the network service and the Storage Service;
   - unregistering at `closeContext`.

   Check that the key appears in no process's arguments or environment (`ps -E`), and in no file.
7. **Engine unit tests.** Add a `pane_unittests` gtest target in `engine/pane` for the layers below, and document how to build and run it in `docs/building.md`.

**Done when:** the baseline and the inventory are committed, the audit runs in report mode, the Spotlight check is settled, the flags are ignored, and the key reaches every process and leaves it at lock, with unit tests.

### Phase 1: SQLite (about 2 weeks)

**The VFS.** A "pane" VFS in `//pane`, registered as the default over Chromium's VFSWrapper:
- **Registration:** call `sql::EnsureSqliteInitialized()`, then `sqlite3_vfs_register(&pane, 1)` with its parent from `sqlite3_vfs_find("VFSWrapper")`. The order matters: `SQLITE_OMIT_AUTOINIT`; `sql/vfs_wrapper.cc:363-382`. Register it in the browser process and the network service, before any database opens.
- **Which files:** chosen by resolved path in `xOpen` (`sqlite/os_unix.c:7008-7033`). Everything else is opened straight on the parent.
- **Memory mapping:** methods version 2, so SQLite never maps the file (`pager.c:3582`). Today every database maps up to 256 MB (`sql/database.cc:2444-2465`).
- **I/O capabilities:** clear `SQLITE_IOCAP_SUBPAGE_READ` (`pager.c:819-823`) and keep power-safe overwrite.
- **Pages:** encrypt exactly the page images.
  - Main file: whole pages at `(pgno-1)·P` (`pager.c:4527, 2441, 2743`; `wal.c:2321`). The 100-byte header read at 0 and the 16-byte read at 24 are served from page 1, decrypted.
  - Rollback journal: records of a page number, a page, and a checksum (`pager.c:6062-6066`).
  - WAL: a 24-byte frame header, then the page (`wal.c:3973-3977`).
  - Journal and WAL files are bound to their main file with `sqlite3_database_file_object`.
  - An I/O shape outside these fails with an I/O error.
- **Reserved bytes:** zeroed on every decrypt. The WAL checksum covers them (`wal.c:985-986`), so a page that arrives with non-zero reserve bytes is refused.
- **Errors:** a page that fails authentication returns `SQLITE_IOERR_AUTH` (main file), never CORRUPT, which Chromium answers with recovery or a raze. In a journal or WAL, it returns a zeroed page, so SQLite's own checksum ends playback. The key is checked at `xOpen`.

**Reserved bytes in every connection.** A Pane `sqlite3_auto_extension` sets `SQLITE_FCNTL_RESERVE_BYTES` to 32 on Pane's files, and on the temporary and in-memory connections that `Raze` (`sql/database.cc:1109-1146`) and `sql::Recovery` back up from. Their backups copy page 1 verbatim (`backup.c:113-115, 278`). Add the small patch that makes Raze use the database's real page size. If the extension proves fragile, fall back to explicit patches at `database.cc:2244`, in Raze, and in recover.

**Migration.** At the profile's first open after the update, in the browser process and before the profile opens:
1. Open the database raw: `sqlite3_open_v2(path, READWRITE | SQLITE_OPEN_URI, "VFSWrapper")`. This settles any leftover journal or WAL in the clear.
2. `VACUUM INTO 'file:<temp>?vfs=pane&reserve=32'`.
3. fsync, then rename over the original.
4. Delete the old `-journal` or `-wal`, and fsync the directory.

The plaintext the migration frees stays inside the encrypted volume.

**Upstream.** Chromium has started `components/sqlite_encrypted_vfs`: a passthrough skeleton with `xFetch` returning NULL. Keep Pane's VFS in `//pane`, shaped like upstream's, so it can move onto it once upstream encrypts.

**Checks:**
- Unit tests:
  - every I/O shape in journal and WAL modes;
  - reserved bytes zero in everything SQLite checksums;
  - a wrong key or a missing one crashes and deletes nothing;
  - Raze, Recovery, and VACUUM on encrypted databases.
- Kill loops: `kill -9` during writes to History (journal mode) and DIPS (WAL), then a relaunch; repeat 200 times with no committed data lost.
- `files-encrypted`: no SQLite file is plain, and the planted URL is in none.
- Scenarios: history, cookies, saved passwords, and autofill work across a relaunch, a lock, and an unlock.
- Budgets: the History query, the cookie load, and warm page loads (memory mapping is now off; raise `cache_size` where measurements ask for it).

**Fallback:** AES-256-XTS per page, which keeps lengths and needs no reserved bytes, but gives no integrity and is deterministic. Only if the reserved-bytes plumbing proves unsafe.

### Phase 2: LevelDB and IndexedDB's blobs (about 2 weeks)

**The hooks.** In ChromiumEnv's file methods (`third_party/leveldatabase/env_chromium.cc`), with the crypto provided by `//pane` through a registry (`leveldatabase` has no `//crypto` dependency):
- **Writes:** `NewWritableFile` (989-1002) writes the header, then encrypts each append at its logical offset.
- **Reads:** `NewSequentialFile` (954-965) and `NewRandomAccessFile` (967-987) shift every read past the header. A file shorter than the header, or with an all-zero header, counts as empty.
- **Appends to an existing file:** `NewAppendableFile` (1004-1016) returns NotSupported, with `reuse_logs` off on Mac (484-496), as on ChromeOS.
- **Logging:** `NewLogger` (942-952) is a no-op: LevelDB's text LOG prints user keys.
- **Sizes:** `GetFileSize` (856-867) returns the logical size, which repair uses.

Hooking `Env::Default` or `OpenDB` instead would miss IndexedDB, DOM storage, and the Service Worker database, which build their own envs, and `RepairDB`, which callers run directly.

**The Storage Service.** It writes Local and Session Storage, out of process and sandboxed (`chrome_content_browser_client.cc:7902-7907`). It gets the key over its mojom, before each `Bind*StorageControl` and again after a restart (`storage_partition_impl.cc:257`; `dom_storage_context_wrapper.cc:350-381`). The other option, running it in the browser process (`GetSandboxedStorageServiceDataDirectory`), drops its sandbox; decide by measurement and stability.

**IndexedDB's blobs.**
- Writes: the three paths in `storage/browser/blob/write_blob_to_file.cc` (158-226, 228-245, 371-392), with an encrypting copy in place of `base::CopyFile` and the modification times kept.
- Reads: the one reader, `file_stream_reader_to_data_pipe.cc` (59-70, 120-126), with offsets shifted.
- Sizes: the code that measures blob files (`backing_store.cc:1343-1352, 1652-1657`).

**IndexedDB's folder names.** They name every site today (`IndexedDB/<origin>.indexeddb.leveldb`). Default buckets move to the `WebStorage/<bucket_id>/IndexedDB` layout (`ShouldUseLegacyFilePath`, `content/browser/indexed_db/file_path_util.cc:52-55`), and existing folders move once. Never turn on `kIdbSqliteBackingStore`: it deletes LevelDB data rather than migrating it.

**Migration.** For each closed database:
- each plaintext file is re-encrypted to a temp file, fsynced, and renamed over the original (LevelDB files never change once written);
- `LOG` and `LOG.old` are deleted;
- blob files are re-encrypted with their modification times kept.

The profile's stores are then strict: plaintext is refused.

**Checks:**
- `pane_unittests` for the hooks.
- If they build in reasonable time, `leveldb_unittests` and `env_chromium_unittests` with encryption forced; the recovery tests need their reuse expectations changed.
- A wrong key or a missing one crashes and deletes nothing.
- Kill the Storage Service mid-session and confirm the key comes back.
- `files-encrypted`: the planted Local Storage and IndexedDB values appear in no file, and no folder names a site.
- The data survives a relaunch, a lock, and an unlock.
- Timings: opening with log reuse off, against on, for IndexedDB and Local Storage.

### Phase 3: caches (about 1 week)

- **HTTP cache.** In `pane::ContentBrowserClient` (`engine/pane/browser/content_browser_client.h`), override `ConfigureNetworkContextParams`. Call Chrome's version, then set `enable_encrypted_http_cache` and an `encryption_provider` built on a per-profile os_crypt_async (`chrome/browser/enterprise/encryption/cache_encryption_provider_impl.cc:20-118`). Turn No-Vary-Search's persistence off (`net/base/features.cc:696, 715-718`): its files are not covered. Fallback: an in-memory cache with a size cap (clear `http_cache_directory`, set `http_cache_max_size`).
- **CacheStorage and the Service Worker ScriptCache.** Allow disk_cache's encryption delegate for any cache type (`net/disk_cache/disk_cache.cc:277-281`), and pass one at `cache_storage_cache.cc:2683` and in `service_worker_disk_cache.cc:271-277`. `index.txt` goes through the whole-file codec. Keying CacheStorage's folder names (`cache_storage_manager.cc:119-130`) is optional, decided by cost.
- **Code cache.** Off at first (`GetGeneratedCodeCacheSettings(false, 0, path)`). If repeat visits suffer beyond budget, encrypt it with the same delegate (`generated_code_cache.cc:631`).
- **GPU caches inside a profile.** Off, with a one-condition patch at `render_process_host_impl.cc:2013`. They use the blockfile format, which maps its files.
- **Shared dictionaries.** Off (`AllowCompressionDictionaryTransport` returns false).

**Checks:** `files-encrypted` (the planted page text appears in no cache file), and repeat-visit loads after a relaunch within budget.

### Phase 4: loose files and the rest (about 2 weeks)

- **The whole-file codec.** It goes in `base`, applied only to paths under a registered profile:
  - the write side: `ImportantFileWriter::WriteFileAtomicallyImpl` (`base/files/important_file_writer.cc:274`) and `base::WriteFile` (`file_util_posix.cc:1439-1458`);
  - the read side: `base::ReadFileToStringWithMaxSize` (`base/files/file_util.cc:375-388`).

  It covers Preferences, Secure Preferences, AccountPreferences, Bookmarks and its `.bak`, TransportSecurity, Web Applications, Custom Dictionary.txt, the DNR dynamic rules, and CacheStorage's `index.txt`. Copies stay ciphertext, since nothing is bound to a path.
- **Bookmarks and sessions.** Turn off Chromium's Keychain-encrypted copies (`kEncryptBookmarks` and the sessions stage): they open without the PIN, and the codec replaces them.
- **Network files.** Network Persistent State and SCT reports go memory-only (`profile_network_context_service.cc:1448-1473`).
- **Tab restore.** Pane's own reopen-closed-tab doesn't use it:
  - Drop `CLOSE_CREATE_HISTORICAL_TAB` (`engine/pane/browser/web_contents.mm:376-379`).
  - Tab restore never persists (`tab_restore_service_impl.cc:1944-1968`).
  - Confirm no `Sessions/Session_*` file appears, since Pane's Browsers are omitted from session restore.
- **The origin private file system.**
  - Encrypt the local stream reader and writer and the disk delegate (`storage/browser/file_system/obfuscated_file_util.cc:311-341`).
  - Route sync access handles through the mojo delegate, as Incognito does (`file_system_access_file_handle_impl.cc:349-354, 384-388`), so renderers never hold a raw descriptor, a key, or ciphertext.
  - Measure the handles' cost.
- **Memory-only.** `blob_storage` (`chrome_blob_storage_context.cc:128`, capped at 2 GiB as in Incognito) and the Download Service (`background_download_service_factory.cc:171`).
- **Stale files.** The migration deletes Visited Links, old `Sessions/`, `Sessions_Encrypted/`, and `EncryptedBookmarks2`.
- **The audit turns strict.**

**Checks:** `files-encrypted` passes strict; bookmarks, preferences, HSTS, and the origin private file system survive a relaunch.

### Phase 5: migration and lifecycle (about 1 week, alongside Phases 1 to 4)

- **One orchestrator.** It runs each layer's migration at a profile's first open after the update, inside the vault's open step (`app/Pane/Security/DataVault.swift`). It is resumable: each file goes temp, then verify by decrypting, then rename, and the original stays until its copy verifies. It is marked done per profile in the sealed `profiles.json`.
- **Progress.** If a migration can take over a second, Pane shows it the way it shows the move into the volume (Paper 07.5). **A new board, or a change to 07.5, is drawn in Paper and approved by the user first.**
- **Lock and quit.** Keys leave every process after the profiles close (ADR 0116). Check that no file is opened after a key goes.
- **Other paths.** Profile deletion, the panic wipe, and the Electron app's migration, which arrives plain and is then encrypted.
- **A synthetic large profile.** Built on the published 0.15.2 through the scenario harness, with thousands of history entries, many cookies, and hundreds of MB of IndexedDB and cache. It times the migration and kills it at random points.

### Phase 6: verification, performance, release (about 1 week)

- **Everything green:** the full suite, PaneKit, `pane_unittests`, lint, typecheck, and knip.
- **The audit across states:** `files-encrypted` strict with three profiles, after a lock and an unlock, after a relaunch, and after a crash.
- **Budgets.** Set against Phase 0's baseline; adjust them only with the user.

  | Measure | Budget |
  |---|---|
  | Warm page load (`benchmark-pages`) | ≤ +3% median |
  | Repeat visit after a relaunch | ≤ +5% |
  | Launch to first frame | ≤ +10 ms |
  | Unlock to profiles open (once migrated) | ≤ +100 ms |
  | History and omnibox query, cookie load | ≤ +10% p95 |
  | Memory (`benchmark-memory`) | ≤ +5% |
  | One-time migration | reported in MB/s; shown as progress past 1 s |

- **The record.** ADR 0119: the decision, what was rejected, the residuals, and the checks; notes at the top of ADRs 0113 and 0115. Add a step to `engine/UPGRADING.md`: on every Chromium upgrade, run `files-inventory` and `files-encrypted`, and look for new stores.
- **Release** (a minor version, 0.16.0), with the user's explicit OK: build the engine, `publish-engine.sh`, tag, watch CI, then check the live update from 0.15.2. Then re-audit the user's own data after they update, reading metadata only, as done for 0.15.1.

## What was rejected

- **Transparent encryption by dyld interposing** (intercepting open, read, write, stat, mmap):
  - Whether dyld honours interposing in hardened-runtime processes is unknown (AMFI).
  - Calls made inside libSystem (`fopen`, `copyfile`, `mkstemp`) may escape it.
  - It has to emulate POSIX exactly: file sizes, `O_APPEND`, `F_BARRIERFSYNC`, descriptors passed to other processes.
  - A torn chunk loses data that was committed.

  It is kept, at most, as a log-only tool for finding writers.
- **A Group Container alone** (after the Developer ID): processes with Full Disk Access still read it. It remains a complement.
- **Everything in memory, as Incognito:** the data would be gone at every quit.
- **A SQLCipher fork:** SQLite removed codec support, and a VFS shim survives upgrades.
- **XTS for SQLite:** no integrity, and deterministic. Only a fallback.
- **IndexedDB's SQLite backend:** it deletes LevelDB data, and is not launched yet.

## Risks, ranked

1. **Data loss through Chromium's corruption handling.**
   - A key or decryption error that reaches it razes, deletes, or rebuilds stores. Mitigations:
     - fail closed (crash);
     - I/O errors rather than corruption errors;
     - key checks in every header;
     - verify before replacing during migration;
     - kill loops in every phase.
2. **Coverage drift.** Each Chromium milestone adds stores. `files-inventory` and `files-encrypted` run on every upgrade (UPGRADING.md), and a new store without a hook fails the audit.
3. **Performance.** Memory mapping off for SQLite, `reuse_logs` off, cold caches. Every phase measures against the budgets.
4. **The key in more processes.** The network service and the Storage Service are both hardened and sandboxed; the key travels in shared memory and leaves at lock.
5. **Conflicts with upstream.** `sqlite_encrypted_vfs`, the encrypted cache, and the bookmarks and sessions encryption stages are all moving. Pane's code lives in `//pane`, the patches stay thin, and UPGRADING.md lists them.
6. **Metadata.** Names, sizes, and times stay readable: accepted, reduced, and recorded.
7. **Writers.** Out of scope until the Developer ID.

## Effort

About 8 weeks of focused work (6 to 10): Phase 0 one, Phases 1 and 2 two each, Phases 3 and 6 one each, Phase 4 two, and Phase 5 alongside. The earlier estimate of 5 to 6 weeks grew with what the research found: the Storage Service's own process, IndexedDB's folder names, the origin private file system's descriptors, and crash safety.

## Gates that need the user

- **New UI:** any new interface (a migration progress board, say) is drawn in Paper and approved first.
- **Publishing:** pushing, uploading the engine, and releasing each wait for the user's OK.
- **The threat model,** if it is to include processes that write.
- **A budget that can't be met,** or a cache that would have to stay off.

## Working rules for whoever carries this out

- **Builds:** run them niced, with `engine/scripts/build.sh` (a third of the cores); the user works on this Mac.
- **The user's things:** never touch `/Applications` or the user's data root. Start Pane only through the scenario harness, on throwaway data. Never send input to the app in front. Never print the user's data.
- **Measurements:** measure before and after, and report the numbers; a regression found is a regression fixed.
- **Commits:** commit at the end of each green phase. Record every decision in this plan's log, and in ADR 0119 at the end.
- **Blockers:** when a phase hits one, take the fallback this plan names. Ask the user only at the gates above.

## Log

- 2026-10-31: plan written from three code reviews of Chromium 157 (SQLite, LevelDB, caches and loose files), with file references checked against the tree. No phase started.
