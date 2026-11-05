# Plan: every profile file unreadable to other processes while Pane is unlocked

Status: proposed 2026-10-31; revised 2026-11-03, after its code references were checked against the trees and its design reviewed (see the log). Being built since 2026-11-05: the log records what each phase found. The decision goes into ADR 0121 (one record per subject, AGENTS.md): 0119 went to another subject while this plan waited, so where this plan says 0119 it means 0121.

## The goal

The user's requirement (2026-10-31): none of Pane's content can be seen without the PIN. ADR 0113 encrypts every profile at rest, in an APFS volume the PIN unlocks, and ADR 0116 locks Pane while open. But while Pane is unlocked the volume is mounted, and any process running as the user reads the profiles' files: history, site storage, caches, preferences. Only cookies and saved passwords are encrypted again, by Chromium, under the Keychain's "Pane Safe Storage" key (ADR 0115 lists this gap).

This plan closes it inside the engine. Every file a profile writes is either encrypted with a per-profile key that only Pane's processes hold in memory, or kept in memory. Another process, even one with Full Disk Access, then reads only ciphertext, while Pane is unlocked as much as while it is locked. Pane's processes run under the hardened runtime (ADR 0115), so their memory, and the key in it, is out of other processes' reach; Phase 0 proves that.

Without a PIN (ADR 0115: the PIN is optional) the files are just as encrypted. The data key then opens with the Keychain's secret alone, and macOS asks the user before any other process reads that item.

### Done when

On a release build, all of these hold:

1. **No plaintext while open.** The `files-encrypted` scenario passes strict.
   - It runs a rich session: sign-ins and cookies, history, Local and Session Storage, IndexedDB with blobs, CacheStorage, a service worker, the origin private file system with a sync access handle, a download, and an extension.
   - Then, with Pane open and unlocked, it checks every file under each profile's folder, its cache folder, the data root, and every file Pane's processes created in `$TMPDIR`. Each is ciphertext, or on the allow-list below.
   - No visited URL, cookie value, stored value, or page text appears in any file, allow-listed ones included.
   - No folder or file name names a site, in clear or as an unkeyed hash.
2. **Nothing lost, nothing destroyed.**
   - A profile written by 0.15.2 migrates on its first open: history, cookies (signed in), saved passwords, site data, extensions, and settings all survive. The migration survives being killed at any point.
   - The published 0.15.2, started on migrated data, opens none of it and changes no file: it says the data won't open (Paper 07.8).
3. **Lifecycle intact.** Lock, unlock, quit, a crash, profile deletion, profile unload, and the panic wipe work as before. Once the lock has detached the volume, every process's key registry is empty, as each process's engine trace reports.
4. **Green.**
   - The full scenario suite (with `files-encrypted` added to CI's list), PaneKit's tests, the engine's new `pane_unittests`, lint, typecheck, and knip all pass.
   - The engine builds clean and publishes (`publish-engine.sh`).
5. **Fast.** The benchmarks stay within the budgets below, against 0.15.2's.
6. **Recorded.** ADR 0119 records the decision, its residuals, and its checks; ADRs 0113 and 0115 point to it. `engine/UPGRADING.md` gains the audit step.
7. **Tried as a person would.** A computer-use pass of the whole QA checklist (Testing as a person would, below) passes on the release candidate, in light and dark, with its report and screenshots committed. It covers:
   - the PIN typed with real keys;
   - Lock Pane from the keyboard and the menu, and the idle lock;
   - real sites, and a sign-in that survives a relaunch;
   - a large profile's migration with its progress;
   - the downgrade fence's screen;
   - the profile's files seen from another app.

## Threat model

**In scope:** a process running as the user (an app, a script, malware, a backup or indexing tool, with or without Full Disk Access) that reads files while Pane is unlocked. That covers the data root, the profiles' cache folders, and `$TMPDIR`.

**Out of scope, recorded as residuals in ADR 0119:**

- **A process that writes.** It can change Pane's bundle, which ad-hoc signing does not guard (no library validation, no `restrict`), and steal the key at the next launch. It can also edit Local State (whose chrome://flags entries become switches) or swap files around. This waits for a Developer ID (ADR 0048): library validation, and macOS's App Management. No encryption depends on a feature flag or a field trial, so none of them can turn it off.
- **Root and the kernel.**
- **The screen and the UI:** Screen Recording and Accessibility, which TCC asks the user for.
- **Apple Events:** Automation, which TCC asks the user for.
- **Pane's processes' memory:** the hardened runtime guards it, as Phase 0 checks.
- **What the user takes out of Pane:**
  - downloads (the user's files);
  - print jobs (macOS spools them);
  - drags and the clipboard;
  - site notifications, which macOS shows and keeps (ADR 0115, the user's call).
- **Metadata:** file sizes and times, and the names of installed extensions' folders (`Extensions/<id>`, the extensions' public code). The phases remove the other names that point at sites: IndexedDB's folders (Phase 2), and every disk cache's entry names and CacheStorage's folders (Phase 3).
- **The engine's Default profile and the data root's own files:** the allow-list below. Chromium opens them before the PIN, and the audit of 0.15.1 found nothing of the user's in them (ADR 0115).

## Design

### The key

- **Derivation.** The app derives a per-profile **engine key**: HKDF-SHA256 of the data key the PIN releases, with the profile's id as salt and `Pane engine files` as info. It is distinct from `DataKey.filesKey` (`app/PaneKit/Sources/PaneKit/DataKey.swift:81-82`), which seals Pane's own files. Nothing writes it to disk, puts it on a command line, or in an environment.
- **Handover.** `openContext` takes the key (a new parameter on `PaneBrowserApplication`). The engine registers it in a per-process registry before `ProfileManager::CreateProfileAsync` (`engine/pane/browser/browser_application.mm:345-405`). The registry holds each profile's protected roots in both forms: the link path (`Engine/<id>`, and the cache's link under `~/Library/Caches`) and the resolved one (`Engine/Volume/<id>/Profile`, `Engine/Volume/<id>/Cache`). SQLite sees resolved paths; other code sees links.
- **Protected paths.** Everything under the engine's volume mount (`<user data dir>/Volume/`) and under the profiles' cache links is protected, except the allow-listed subpaths. A protected path with no key in the process **crashes** it. It never returns an error: Chromium deletes or razes stores that fail to open (`sql/error_delegate_util.cc:59-98`; IndexedDB's `bucket_context.cc:1106-1122`; Local Storage's `local_storage_impl.cc:565-609`; leveldb_proto's `leveldb_database.cc:87-93`).
- **Other processes.** The key reaches the network service and the Storage Service in a shared memory region, never in a mojo buffer, as os_crypt_async does (`components/os_crypt/async/common/encryptor.mojom:13-32`).
  - The network service gets it in `NetworkContextParams`: a patched field, registered in `NetworkContext`'s constructor before the cookie store loads. Pane's `ConfigureNetworkContextParams` adds it each time, so a network service that restarts gets it back.
  - The Storage Service gets it in its own mojom (Phase 2).
- **Lifetime.** The key stays registered while Pane is unlocked, profile unloads included (ADR 0041). Each process drops it only once the lock's detach has succeeded: no Pane process has a profile file open by then. Dropping it earlier would crash stores that still flush as their profile is destroyed. It is also dropped at quit.

### What each kind of file gets

| Store | Process | Treatment | Hook |
|---|---|---|---|
| SQLite: History, Favicons, Top Sites, Shortcuts, Web Data, Account Web Data, Login Data (+ For Account), Network Action Predictor, Affiliation Database, QuotaManager, DIPS, CdmStorage.db, heavy_ad_intervention_opt_out.db, declarative_performance_observer.db, MediaDeviceSalts, ServerCertificate, PrivateVerificationTokens, Extension Cookies | browser | encrypted | Pane VFS (Phase 1) |
| SQLite: `Network/` Cookies, Reporting and NEL, Trust Tokens, Device Bound Sessions | network service | encrypted | Pane VFS in the network service |
| LevelDB: IndexedDB, Service Worker database, Platform Notifications, shared_proto_db, GCM Store, Sync Data, Site Characteristics Database, extension state, rules, scripts and settings, File System metadata | browser | encrypted | ChromiumEnv hooks (Phase 2) |
| LevelDB: Local Storage, Session Storage | Storage Service | encrypted | ChromiumEnv hooks, key over mojo |
| IndexedDB blob files | browser (blob subsystem) | encrypted | `write_blob_to_file.cc`, `file_stream_reader_to_data_pipe.cc` |
| HTTP cache | network service | encrypted, keyed entry names | Chromium's encrypted cache, per-profile provider (Phase 3) |
| CacheStorage and its `index.txt`; Service Worker ScriptCache | browser | encrypted, keyed entry and folder names | disk_cache encryption delegate, generalised; whole-file codec |
| Code cache (js, wasm) | browser | off, then encrypted if repeat visits suffer | `GetGeneratedCodeCacheSettings` |
| GPU caches in a profile (GPUCache, DawnWebGPUCache, DawnGraphiteCache) | browser | off | patch at `render_process_host_impl.cc:2008-2016` |
| Shared dictionaries | network service | off | `AllowCompressionDictionaryTransport` returns false |
| Preferences, Secure Preferences, AccountPreferences, Bookmarks (+ `.bak`), TransportSecurity, Web Applications, Custom Dictionary.txt, DNR dynamic rules | browser, network | encrypted | whole-file codec (Phase 4) |
| Network Persistent State, SCT Auditing Pending Reports | network service | memory-only | their names cleared in Pane's `ConfigureNetworkContextParams` |
| Tab restore (`Sessions/Tabs_*`), `Sessions_Encrypted`, `EncryptedBookmarks2` | browser | off | drop `CLOSE_CREATE_HISTORICAL_TAB`; tab restore never persists; the Keychain-encrypted copies off |
| File System / origin private file system | browser; renderers with sync access handles | encrypted | stream reader and writer, disk delegate; sync handles through the mojo delegate |
| blob_storage, Download Service | browser | memory-only | one-condition patches |
| Visited Links | none in 157 (partitioned, built from History) | stale file deleted | migration |
| Extensions' code (`Extensions/`) | browser, unzip utility | allow-listed | |

### The allow-list

These may stay plain; `files-encrypted` still searches them for the planted values. Any addition needs a reason recorded here and in ADR 0119.

- **The data root:**
  - Pane's own files: `settings.json`, readable at the user's call; `security.json` and `Data.security.json`, which hold the PIN's check and the sealed key; `migration.json`; `profiles.json`, sealed; `Pane.lock`.
  - The volume's image, `Data.sparsebundle`.
- **The engine's root** (`Engine/`), which Chromium uses before the PIN:
  - `Local State`, `Last Version`, `Variations`, `ChromeFeatureState`;
  - the GPU's shader caches: `GraphiteDawnCache`, `GPUPersistentCache` (ADR 0115);
  - `Safe Browsing`, `component_crx_cache`, `extensions_crx_cache`, `Webstore Downloads`, `NativeMessagingHosts`;
  - the `Default` profile, holding only Chromium's defaults (ADR 0115).
- **In a profile:** `Extensions/` (installed extensions' public code), and empty files and folders.

### Formats

All of them use BoringSSL, already in Chromium; AES runs in hardware on Apple silicon.

- **SQLite.** 32 reserved bytes per page: a 12-byte random nonce, a 16-byte AES-256-GCM tag, and 4 bytes of format and key id.
  - **Authenticated data:** the kind of file and the page number (main file) or offset (journal, WAL), never the path, since Chromium copies database files.
  - **Page 1:** bytes 0-15 carry a Pane magic in place of "SQLite format 3\0" (restored on read), and bytes 16-23 stay clear and authenticated. SQLite's own magic marks a legacy plaintext file; an empty file is a new encrypted one.
- **LevelDB and stream files** (IndexedDB blobs, the origin private file system).
  - **Header:** 32 bytes: magic, version, flags, a 16-byte random file id, and an 8-byte key check.
  - **Per-file key:** HKDF-SHA256 of the engine key, with the file id as salt and, as info, the store's directory relative to the profile, normalised for `__tmp_for_rebuild` and `lost/`.
  - **Encryption:** AES-256-CTR, the counter being the logical block index. LevelDB never rewrites a byte in place once appends are refused (below). A file fails for good after any failed write, so a keystream is never reused.
- **Whole files.** A magic and a version, then a random nonce, then AES-256-GCM over the whole content. These files are small and rewritten whole (ImportantFileWriter writes a temp file and renames it).
- **Caches.** Chromium's own chunked format: AES-256-GCM-SIV in 4 KiB chunks with a per-file HKDF key (`services/network/enterprise/encryption/chunked_encryptor.h`), and keyed HMAC entry names.

## Phases

Each phase ends green, is committed, and adds its findings to the log. Green means its checks, the scenarios it touches, and a computer-use pass of the QA checklist's items for what it changed (below). Estimates are for one agent working full time, measured where they can be.

### Phase 0: groundwork (about 1 week)

1. **Baseline.** Measure 0.15.2's build with the existing benchmarks, and record the numbers in `docs/benchmarks/engine-encryption.md`:
   - `benchmark-pages`: warm loads, and repeat visits after a relaunch.
   - `benchmark-launch`, `benchmark-unlock`, `benchmark-memory`.
   - New micro-benchmarks: a History and omnibox query, the cookie load at startup, IndexedDB and Local Storage reads and writes.
2. **Inventory.** A `files-inventory` scenario runs the rich session and lists every file under the profile, its cache folder, the data root, and what Pane's processes create in `$TMPDIR` (a before-and-after diff). For each it notes the kind, the writer, and whether its content is plain. This list becomes the coverage checklist, and it settles whether `Sessions/Session_*` appears.
3. **Audit scenario.** `files-encrypted` runs the same session, then scans every file while Pane is open:
   - It looks for markers: SQLite and LevelDB headers, JSON, HTML, and URLs.
   - It checks entropy (at least 7.9 bits a byte for files over 4 KiB).
   - It searches for the exact strings the session planted: a URL, a cookie value, stored values.
   - It checks the names: no site in clear, no unkeyed hash of a visited origin or URL.

   It starts in report mode, which lists today's plaintext. It turns strict once Phase 4 ends, with the allow-list above.
4. **The memory claim.** On a scenario's copy, confirm that a process of the same user can't read the memory of Pane's processes: `lldb -p`, `vmmap`, and `heap` must all fail on the browser process, the network service, and the Storage Service. The plan rests on this.
5. **Spotlight.** With Pane open, check whether Spotlight indexes the mounted volume (`mdutil -s` on the mount point, `mdfind -onlyin`). If it does, or if that can't be told, pick the mechanism the check proves works and apply it at every mount (`DataVolume.swift`): a `.metadata_never_index` at the volume's root, say. Then confirm.
6. **Key spike.** Build the key's path end to end, for the browser and the network service, with an encryptor that changes nothing:
   - the app's derivation;
   - `openContext` with the key;
   - the registry, by link and resolved path;
   - the shared-memory handover in `NetworkContextParams`, surviving a network service restart;
   - dropping the key after the lock's detach.

   Check that the key appears in no process's arguments or environment (`ps -E`), and in no file.
7. **Engine unit tests.**
   - Add a `pane_unittests` gtest target in `engine/pane` for the layers below.
   - CI never builds Chromium, so these run locally. Add them to AGENTS.md's checks before committing an engine change, and document them in `docs/building.md`.
8. **The QA rig.** Set up the computer-use rig (below). Run the checklist once on 0.15.2's build, so later passes compare against its screenshots, recordings, and timings.

**Done when:**
- the baseline, the inventory, and the QA rig's first pass are committed;
- the audit runs in report mode;
- the memory claim and the Spotlight check are settled;
- the key reaches both processes and leaves them after a lock, with unit tests.

### Phase 1: SQLite (about 2 weeks)

**The VFS.** A "pane" VFS in `//pane`, registered as the default over Chromium's VFSWrapper:
- **Registration:** call `sql::EnsureSqliteInitialized()`, then `sqlite3_vfs_register(&pane, 1)` with its parent from `sqlite3_vfs_find("VFSWrapper")`. The order matters: `SQLITE_OMIT_AUTOINIT`; `sql/vfs_wrapper.cc:360-385`. Register it in the browser process and the network service, before any database opens.
- **Which files:** chosen by resolved path in `xOpen` (`sqlite/os_unix.c:7008-7033`). Everything else is opened straight on the parent.
- **Memory mapping:** methods version 2, so SQLite never maps the file (`pager.c:3582`). Today every database maps up to 256 MB (`sql/database.cc:2440-2470`).
- **I/O capabilities:** clear `SQLITE_IOCAP_SUBPAGE_READ` (`pager.c:819-823`) and keep power-safe overwrite.
- **Pages:** encrypt exactly the page images.
  - Main file: whole pages at `(pgno-1)·P` (`pager.c:4527, 2441, 2743`; `wal.c:2321`). The 100-byte header read at 0 and the 16-byte read at 24 are served from page 1, decrypted. Keep page 1's plaintext per open file, invalidated on write: the databases on NORMAL locking (Cookies, Reporting and NEL) read those 16 bytes at every transaction.
  - Rollback journal: records of a page number, a page, and a checksum (`pager.c:6062-6066`).
  - WAL: a 24-byte frame header, then the page (`wal.c:3973-3977`).
  - Journal and WAL files are bound to their main file with `sqlite3_database_file_object`.
  - An I/O shape outside these fails with an I/O error.
- **Reserved bytes:** zeroed on every decrypt. The WAL checksum covers them (`wal.c:985-986`), so a page that arrives with non-zero reserve bytes is refused.
- **Errors:** a page that fails authentication returns `SQLITE_IOERR_AUTH` (main file), never CORRUPT, which Chromium answers with recovery or a raze. In a journal or WAL, it returns a zeroed page, so SQLite's own checksum ends playback. The key is checked at `xOpen`.

**Reserved bytes in every connection.** A Pane `sqlite3_auto_extension` sets `SQLITE_FCNTL_RESERVE_BYTES` to 32 on Pane's files, and on the temporary and in-memory connections that `Raze` (`sql/database.cc:1105-1150`) and `sql::Recovery` back up from. Their backups copy page 1 verbatim (`backup.c:113-115, 278`). Add the small patch that makes Raze use the database's real page size. If the extension proves fragile, fall back to explicit patches at `database.cc:2244`, in Raze, and in recover.

**Migration** (run by Phase 5's orchestrator):
1. Open the database raw: `sqlite3_open_v2(path, READWRITE | SQLITE_OPEN_URI, "VFSWrapper")`. This settles any leftover journal or WAL in the clear.
2. `VACUUM INTO 'file:<temp>?vfs=pane&reserve=32'`.
3. fsync, then rename over the original.
4. Delete the old `-journal` or `-wal`, and fsync the directory.

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

**The hooks.** In ChromiumEnv's file methods (`third_party/leveldatabase/env_chromium.cc`), with the crypto provided by `//pane` through the registry (`leveldatabase` has no `//crypto` dependency):
- **Writes:** `NewWritableFile` (985-1005) writes the header, then encrypts each append at its logical offset.
- **Reads:** `NewSequentialFile` and `NewRandomAccessFile` shift every read past the header. A file shorter than the header, or with an all-zero header, counts as empty.
- **Appends to an existing file:** `NewAppendableFile` (1000-1020) returns NotSupported, with `reuse_logs` off on Mac (480-500), as on ChromeOS.
- **Logging:** `NewLogger` (938-955) is a no-op: LevelDB's text LOG prints user keys.
- **Sizes:** `GetFileSize` (850-870) returns the logical size, which repair uses.

Hooking `Env::Default` or `OpenDB` instead would miss IndexedDB, DOM storage, and the Service Worker database, which build their own envs, and `RepairDB`, which callers run directly.

**The Storage Service.** It writes Local and Session Storage, out of process and sandboxed (`chrome_content_browser_client.cc:7902-7907`). It gets the key over its mojom, before each `Bind*StorageControl` and again after a restart (`storage_partition_impl.cc:257`; `dom_storage_context_wrapper.cc:350-381`). The other option, running it in the browser process (`GetSandboxedStorageServiceDataDirectory`), drops its sandbox; decide by measurement and stability.

**IndexedDB's blobs.**
- Writes: the three paths in `storage/browser/blob/write_blob_to_file.cc` (158-226, 228-245, 371-392), with an encrypting copy in place of `base::CopyFile` and the modification times kept.
- Reads: the one reader, `file_stream_reader_to_data_pipe.cc` (59-70, 120-126), with offsets shifted.
- Sizes: the code that measures blob files (`backing_store.cc:1343-1352, 1652-1657`).

**IndexedDB's folder names.** They name every site today (`IndexedDB/<origin>.indexeddb.leveldb`). Default buckets move to the `WebStorage/<bucket_id>/IndexedDB` layout (`ShouldUseLegacyFilePath`, `content/browser/indexed_db/file_path_util.cc:50-57`), and existing folders move once. Never turn on `kIdbSqliteBackingStore`: it deletes LevelDB data rather than migrating it.

**Migration** (run by Phase 5's orchestrator). For each closed database:
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

- **HTTP cache.** In `pane::ContentBrowserClient` (`engine/pane/browser/content_browser_client.h`), override `ConfigureNetworkContextParams`. Call Chrome's version, then set `enable_encrypted_http_cache` and an `encryption_provider` built on a per-profile os_crypt_async (`chrome/browser/enterprise/encryption/cache_encryption_provider_impl.cc:20-118`). The provider holds a Pane key provider over the engine key. That gives encrypted chunks and keyed entry names. Turn No-Vary-Search's persistence off (`net/base/features.cc:696, 715-718`): its files are not covered. Fallback: an in-memory cache with a size cap (clear `http_cache_directory`, set `http_cache_max_size`).
- **CacheStorage and the Service Worker ScriptCache.**
  - Allow disk_cache's encryption delegate, with the keyed entry hasher, for any cache type (`net/disk_cache/disk_cache.cc:274-284`).
  - Pass one at `cache_storage_cache.cc:2678-2688` and in `service_worker_disk_cache.cc:271-277`.
  - `index.txt` goes through the whole-file codec.
  - Key CacheStorage's folder names (an HMAC under the engine key in place of `cache_storage_manager.cc:119-130`'s plain SHA-1 of the origin), and move existing folders once.
- **Code cache.** Off at first (`GetGeneratedCodeCacheSettings(false, 0, path)`). If repeat visits suffer beyond budget, encrypt it with the same delegate (`generated_code_cache.cc:631`).
- **GPU caches inside a profile.** Off, with a one-condition patch at `render_process_host_impl.cc:2008-2016`. They use the blockfile format, which maps its files.
- **Shared dictionaries.** Off.

**Checks:** `files-encrypted` (the planted page text appears in no cache file, and no entry name is an unkeyed hash of a visited URL), and repeat-visit loads after a relaunch within budget.

### Phase 4: loose files and the rest (about 2 weeks)

- **The whole-file codec.** It goes in `base`, applied only to protected paths:
  - the write side: `ImportantFileWriter::WriteFileAtomicallyImpl` (`base/files/important_file_writer.cc:270-278`) and `base::WriteFile` (`file_util_posix.cc:1439-1458`);
  - the read side: `base::ReadFileToStringWithMaxSize` (`base/files/file_util.cc:372-390`).

  It covers Preferences, Secure Preferences, AccountPreferences, Bookmarks and its `.bak`, TransportSecurity, Web Applications, Custom Dictionary.txt, the DNR dynamic rules, and CacheStorage's `index.txt`. Copies stay ciphertext, since nothing is bound to a path.

  `base` is under everything, so a change there rebuilds almost the whole engine (about 2 hours). Land it once, as hook pointers that `//pane` fills; later changes then stay in `//pane`.
- **Bookmarks and sessions.** Turn off Chromium's Keychain-encrypted copies (`kEncryptBookmarks` and the sessions stage): they open without the PIN, and the codec replaces them.
- **Network files.** Network Persistent State and SCT reports go memory-only (`profile_network_context_service.cc:1445-1475`).
- **Tab restore.** Pane's own reopen-closed-tab doesn't use it:
  - Drop `CLOSE_CREATE_HISTORICAL_TAB` (`engine/pane/browser/web_contents.mm:370-382`).
  - Tab restore never persists (`tab_restore_service_impl.cc:1940-1970`).
  - Confirm no `Sessions/Session_*` file appears, since Pane's Browsers are omitted from session restore (`window_host.mm:205-215`).
- **The origin private file system.**
  - Encrypt the local stream reader and writer and the disk delegate (`storage/browser/file_system/obfuscated_file_util.cc:311-341`).
  - Route sync access handles through the mojo delegate, as Incognito does (`file_system_access_file_handle_impl.cc:345-356, 384-388`), so renderers never hold a raw descriptor, a key, or ciphertext.
  - Measure the handles' cost.
- **Memory-only.** `blob_storage` (`chrome_blob_storage_context.cc:124-132`, capped at 2 GiB as in Incognito) and the Download Service (`background_download_service_factory.cc:168-174`).
- **Stale files.** The migration deletes Visited Links, old `Sessions/`, `Sessions_Encrypted/`, and `EncryptedBookmarks2`.
- **The audit turns strict.**

**Checks:** `files-encrypted` passes strict; bookmarks, preferences, HSTS, and the origin private file system survive a relaunch.

### Phase 5: migration, the downgrade fence, and lifecycle (about 1.5 weeks, alongside Phases 1 to 4)

- **Where.** Migration runs per profile, before its first context opens, in the step that already prepares each profile's folders off the main thread: `DataVault.prepare(profile:)` (`app/Pane/Security/DataVault.swift:240-258`). It calls one engine function, safe off the main thread, that runs each layer's migration on the closed profile.
  - The profile that shows opens first.
  - The others migrate in the background before anything opens them. Opening one mid-migration waits for it.
- **Resumable.** Each file is written to a temp file, verified by decrypting it, then renamed. The original stays until its copy verifies. Progress is recorded per profile in the sealed `profiles.json`.
- **A safety copy.** Before a profile's first file converts, Pane writes an archive of its folder, encrypted whole under the engine key, into the volume. It is kept until the profile has opened three times with no store failing. A failure in that window restores it; the window's end erases it.
- **The downgrade fence.**
  - Before the first file of any profile converts, Pane changes the volume's passphrase (`diskutil apfs changePassphrase`) to one derived under a new label. Pane remembers the change and tries the new passphrase first.
  - 0.15.x then can't unlock the volume, and shows its "data won't open" screen (07.8) instead of handing encrypted stores to an engine that would raze them.
  - The check: the published 0.15.2, started on migrated data through the scenario harness, opens nothing and changes no file.
- **Progress.** If a migration can take over a second, Pane shows it, as it shows the move into the volume (Paper 07.5). **A new board, or a change to 07.5, is drawn in Paper and approved by the user first.**
- **Lock and quit.** Keys leave every process after the lock's detach (see The key). Each process's registry reports empty in its engine trace, and `lock-while-open` checks it.
- **Other paths.** Profile deletion and profile unload; the panic wipe, which still erases the image and the key; the Electron app's migration, whose data arrives plain and is then encrypted.
- **A synthetic large profile.** Built on the published 0.15.2 through the scenario harness, with thousands of history entries, many cookies, and hundreds of MB of IndexedDB and cache. It times the migration and kills it at random points.

### Phase 6: verification, performance, release (about 1 week)

- **Everything green:** the full suite, PaneKit, `pane_unittests`, lint, typecheck, and knip. Add `files-encrypted` to CI's scenario list (`.github/workflows/app.yml`).
- **The audit across states:** `files-encrypted` strict with three profiles, after a lock and an unlock, after a relaunch, and after a crash.
- **The whole QA checklist by computer use** on the release candidate, in light and dark, against 0.15.2's pass from Phase 0. Every finding is fixed and checked again the same way.
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

## Testing as a person would: computer use

Scenarios prove the mechanics, through the automation channel. A person checks what they see and do:
- real clicks and keys, through macOS;
- the screens, against Paper;
- real sites;
- the flows that take time: the idle lock, a large migration.

Whoever carries this plan out does both, and drives the app itself by computer use.

### The rig

- **cua-driver** (installed, 0.38.0; its skill, and the repo's wrapper `tests/lib/cua.ts`): the accessibility tree, clicks and typing through element tokens, menus, real keystrokes, and window screenshots.
- **Captures without taking the front:**
  - `screencapture -l <window id>` for stills, as the lock's on-screen check of 2026-10-31 did;
  - `screencapture -v` for short recordings, where motion matters: the PIN screen's slide, a migration's progress.
- **The automation channel, for setup only:** making profiles, planting values, scaling time (`PANE_TIME_SCALE`). It never stands in for the input being tested.
- **A folder per pass:** screenshots, recordings, and a report. The report and the screenshots (fixtures only, nothing of the user's) are committed under `docs/plans/qa/engine-file-encryption/`. Recordings stay local; the report names them.

### When it may take the screen

The user works on this Mac, and a run never takes the front from the app in use (ADR 0012).

- **Always a test copy.** The build under test runs through the scenario harness, on throwaway data. Never the user's Pane, `/Applications`, the user's data, or the user's accounts.
- **Background first.** Whatever accessibility does on a window that isn't in front runs at any time: element clicks, menus, typing into a field, window captures.
- **The front, only when allowed.** What needs Pane in front (real keystrokes such as ⌃⌘L and the PIN, a sheet, fullscreen) runs only when either holds:
  - the user has said so in the session;
  - the Mac has been idle for at least 10 minutes, with its screen unlocked (`HIDIdleTime` from `ioreg -c IOHIDSystem`, `CGSSessionScreenIsLocked`).

  Before such a pass, the agent:
  - notifies the user what will run, for how long, and that any key or click stops it;
  - checks the idle time again before each step, and stops at the first sign of the user;
  - puts back, at the end, the app that was in front.
- **If neither comes.** When a foreground pass is due and neither condition comes within a working day, ask once.

### The checklist

Each phase runs the items its change touches. Phase 6 runs them all on the release candidate, in light and dark.

1. **Lock and unlock.**
   - The PIN typed with real keys, and a wrong one.
   - Lock Pane by ⌃⌘L and from the menu.
   - The idle lock and the lock at the Mac's sleep (time scaled).
   - The PIN screen against Paper 07.1 to 07.9.
   - The tab that showed comes back.
   - From a recording, the time from the keypress to the PIN screen's first frame.
2. **Real browsing,** on throwaway profiles.
   - Public sites that need no account: an encyclopedia, a video, a map, an offline-capable web app.
   - The sign-in fixture: signed in, and still signed in after a relaunch and after a lock.
   - The address bar suggesting history after a relaunch.
   - A download through the UI.
   - An extension added from the Web Store through its own buttons.
3. **Migration.**
   - Phase 5's large 0.15.2 profile, opened with the new build: its progress against its approved Paper board.
   - Killed midway, then relaunched.
   - Everything there afterwards, checked through the UI: history, signed-in sites, site data, extensions.
4. **The downgrade fence.** The published 0.15.2 on migrated data: its screen matches 07.8, and no file changed.
5. **The files from another app.** While Pane is open, as a person poking at the folder would:
   - Finder's Quick Look, and `strings` and `sqlite3` in Terminal;
   - on History, Cookies, Local Storage, an IndexedDB file, and a cache file.

   Each shows ciphertext; the screenshots go in the report.
6. **Speed as felt.** Recordings of the same page loads and repeat visits on 0.15.2 and on the new build, timed frame by frame, beside the benchmarks.

Every finding is fixed, then checked again the same way.

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
- **Ignoring Local State's chrome://flags entries:** it guards only against a process that writes, and such a process can change the bundle anyway.
- **Dropping keys as each profile closes:** stores still flush as their profile is destroyed, and the fail-closed rule would crash them.

## Risks, ranked

1. **Data loss through Chromium's corruption handling.** A key or decryption error that reaches it razes, deletes, or rebuilds stores. Mitigations:
   - fail closed (crash);
   - I/O errors rather than corruption errors;
   - key checks in every header;
   - verify before replacing during migration;
   - the safety copy;
   - kill loops in every phase.
2. **An older Pane on migrated data.** Its engine would raze the encrypted stores. The downgrade fence keeps it out, and Phase 5 checks it with the published 0.15.2.
3. **Coverage drift.** Each Chromium milestone adds stores. `files-inventory` and `files-encrypted` run on every upgrade (UPGRADING.md) and in CI, and a new store without a hook fails the audit.
4. **Performance.** Memory mapping off for SQLite, `reuse_logs` off, cold caches. Every phase measures against the budgets.
5. **The key in more processes.** The network service and the Storage Service are both hardened and sandboxed; the key travels in shared memory and leaves after the lock's detach.
6. **Conflicts with upstream.** `sqlite_encrypted_vfs`, the encrypted cache, and the bookmarks and sessions encryption stages are all moving. Pane's code lives in `//pane`, the patches stay thin, and UPGRADING.md lists them.
7. **Metadata.** Sizes, times, and extensions' folder names stay readable: accepted and recorded.
8. **Writers.** Out of scope until the Developer ID.

## Effort

About 9 to 10 weeks of focused work (7 to 11):

| Phase | Weeks |
|---|---|
| 0 | 1 |
| 1 | 2 |
| 2 | 2 |
| 3 | 1 |
| 4 | 2 |
| 5 | 1.5, alongside 1 to 4 |
| 6 | 1 |
| QA passes by computer use | about 1, across the phases |

The first estimate (5 to 6 weeks) grew with what the research and this review found: the Storage Service's own process, IndexedDB's and CacheStorage's names, the origin private file system's descriptors, crash safety, the safety copy, the downgrade fence, and the computer-use passes.

## Gates that need the user

- **New UI:** any new interface (a migration progress board, say) is drawn in Paper and approved first.
- **Publishing:** pushing, uploading the engine, and releasing each wait for the user's OK.
- **The threat model,** if it is to include processes that write.
- **Scope:** the extensions' folders. Hiding which extensions are installed means encrypting `Extensions/` through the unzip utility and the extension loaders, about a week more.
- **A budget that can't be met,** or a cache that would have to stay off.
- **Taking the screen:** a foreground computer-use pass runs only on an idle Mac or with the user's say-so. If neither comes, ask once.

## Working rules for whoever carries this out

- **Builds:** run them niced, with `engine/scripts/build.sh` (a third of the cores); the user works on this Mac. Batch the changes to `base` (Phase 4), which rebuild nearly everything.
- **The user's things:** never touch `/Applications` or the user's data root. Start Pane only through the scenario harness, on throwaway data. Never send input to the app in front. Never print the user's data.
- **Measurements:** measure before and after, and report the numbers; a regression found is a regression fixed.
- **Commits:** commit at the end of each green phase. Record every decision and finding in this plan's log, and in ADR 0119 at the end.
- **Computer use:** follow the rig's rules (Testing as a person would). Use only test copies and throwaway data, work in the background first, take the front only when idle or allowed, and stop at the first sign of the user.
- **Blockers:** when a phase hits one, take the fallback this plan names. Ask the user only at the gates above.

## Log

- 2026-10-31: plan written from three code reviews of Chromium 157 (SQLite, LevelDB, caches and loose files). No phase started.
- 2026-11-03: review.
  - 26 code references checked against the trees, all holding (line ranges widened where they had drifted).
  - Fixed:
    - the key's name, which collided with `DataKey.filesKey`;
    - when keys drop: after the lock's detach, not as profiles close;
    - the network service getting its key back after a restart;
    - the protected-path rule;
    - an allow-list the audit can be strict against;
    - keyed names for every cache and for CacheStorage's folders;
    - `$TMPDIR` in the audit;
    - where migration runs: per profile, in `DataVault.prepare(profile:)`.
  - Added:
    - a check that the hardened runtime keeps memory out of reach;
    - the downgrade fence, since 0.15.2 would raze migrated stores;
    - a safety copy before migration;
    - CI running the audit;
    - a note on the `base` rebuild;
    - the no-PIN case;
    - what the user exports, as out of scope.
  - Dropped: ignoring Local State's flags.
  - At the user's request, added testing by computer use:
    - a QA rig on cua-driver;
    - a checklist run per phase, and in full on the release candidate (Done when, item 7);
    - the rules for taking the screen on a Mac in use.
  - No phase started.
- 2026-11-05: Phase 0.
  - **Baseline** of the published 0.15.2 in `docs/benchmarks/engine-encryption.md`. The repo has moved on to 0.17.2 since this plan: 0.15.2 stays the baseline the budgets are set against, as it was approved, and migration and the fence are checked from the latest release as well, since that is what users hold.
  - **Inventory** (`files-inventory`, `docs/plans/qa/engine-file-encryption/phase-0/inventory-0.15.2.json`): 357 files. No `Sessions/Session_*` appears, nothing new in `$TMPDIR`. Stores this plan's table left out: `Shared Dictionary/db` (SQLite) and its cache (off with shared dictionaries), `BookmarkMergedSurfaceOrdering`, `PreferredApps`, and `README` (whole files, through `base`), and more empty LevelDB folders. In a scenario's home the caches stay in the profile's folder; only a real Mac's are the link under `~/Library/Caches`, so Phase 6 checks that on a real update.
  - **Audit** (`files-encrypted`) in report mode finds, on 0.15.2, 96 plain files, 17 holding a planted value, and 12 names giving a site away, the Simple cache's hash of the page's cache key among them: the control that it looks in the right places.
  - **Saved passwords:** Chrome's password manager is off in Pane (`password-manager-off`), so `Login Data` holds its schema and no row. The plan's "saved passwords survive" is checked as that file surviving, with a row planted in it.
  - **The memory claim does not hold as the plan wrote it.** `lldb` is refused on the browser, the network service, and the Storage Service, and a program of the user's own gets no task port (`task_for_pid`, `task_read_for_pid`). But `vmmap`, `heap`, `leaks`, `stringdups`, and `sample` all run: Apple's tools carry `com.apple.system-task-ports.read`. They print regions, class names, and stacks, and a value planted in the process appears in none of it (and a memgraph is 3 MB: metadata). The user chose, 2026-11-05, to go on and record this as a residual. `memory-guarded` checks the refined claim.
  - **Spotlight:** `mdutil -s` says "unknown indexing state" on a volume out of the Finder, and `mdfind -onlyin` finds nothing. Since that can't be told for certain, every mount puts `.metadata_never_index` at the volume's root (`DataVolume.mount`); `volume-unindexed` checks both.
  - **The key's path** (`engine-keys`): derived in the app, registered before the profile opens, in shared memory to the network service and the Storage Service; each restarts with its key back and loses nothing (killed with SIGKILL, the cookie and the local storage still there); on no command line, in no environment (`ps -E`), in no file; every registry 0 in the trace after the lock's detach; the PIN brings it all back. At quit the keys go with the processes: dropping them earlier would crash what flushes at shutdown.
  - **Decisions on the way, from reading the code:**
    - The stream format binds no directory: binding guards only against a process that writes, which is out of scope, and a rename across folders would crash a store. The normalising the plan needed for it goes too.
    - Page 1's plaintext is not cached per file: a whole-page read and decrypt costs microseconds, and a stale cache would hide another connection's write. If `benchmark-stores` asks for it, it comes back.
    - The hook in `base` is one new header, included by three `.cc` files, so it rebuilds `base`'s objects and links, not the 2 hours this plan feared. The mojom fields cost 1,500 build steps, once.
  - **The QA rig** (`qa-hold`, `qa-send`, `docs/plans/qa/engine-file-encryption/phase-0/report.md`): first pass on 0.15.2. Background items done: PIN screen, wrong and right PIN by accessibility, a real site, the files read from another program. Owed with the first foreground pass, since the Mac has been in use: real keys, ⌃⌘L, the menu (macOS disables an app's menu behind the front one), sign-in, suggestions, download and Web Store through the UI, Quick Look, recordings.
