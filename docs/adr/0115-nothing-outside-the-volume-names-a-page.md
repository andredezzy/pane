# Nothing outside the encrypted volume names a page, and only a scenario's launch takes Pane's test hooks

> **On the Views line** (`docs/plans/views-migration.md`, H5): one function reads every scenario hook (`model::TestHook`, `pane/model/test_hooks.h`), on the mock keychain alone, where four copies did; the automation channel's directory and the foreground hook had been read off the mock keychain too.

> **Extended by [ADR 0121](0121-every-profile-file-encrypted-under-its-own-key.md)**: what this lists as readable by another process while Pane is unlocked, the profiles' files, no longer is. What it keeps outside the volume stays as it is here.

> **On the Views line** (`docs/plans/views-migration.md`, H7): `PANE_NEW_TAB_URL`, a scenario's new tabs' start page, so its runs load nothing from Google, is one more hook through the same function.

> **On the Views line** (`docs/plans/views-migration.md`, L3): `PANE_MOCK_KEYCHAIN_DENIES`, the mock keychain refusing the engine's item as a denied one (Paper 07.7), is one more hook through the same function. The audit ran again on the Views build packaged with the hardened runtime, and on the published 0.19.2 as a test copy on the same Mac: `private-traces`, `volume-unindexed`, `files-encrypted`, `memory-guarded` (lldb's attach refused, no task port), and `security` pass on both, the same files by kind, and behind the Lock `views-lock-disk` finds nothing of a session in any of either's 151 files, a value planted as its control found.

The user's requirement (2026-10-31): all of Pane's content is encrypted under the PIN, and none of it can be seen without it, whether by another process of the user's, by someone with a copy of the disk, or by someone at the Mac while Pane is locked. Two audits looked for what still could be:

- **Four auditors on 0.14.1**, two on Claude Opus and two on Fable, each pair covering the data at rest and the runtime, read the code and ran throwaway scenarios.
- **The audit of the user's installed 0.15.1**, once it had moved their data into the volume, which the user asked for to check that the cache and all of the browser's content, pages and cookies included, were encrypted. It read the files for names, kinds, sizes, and counts, never their content, and tried the volume without the PIN.

What they found that Pane controls is closed here; what it does not is listed below, with whose call it is.

What changes:

- **Only a scenario's launch takes Pane's test hooks** (`app/Pane/Security/TestHooks.swift`). A scenario runs on the mock keychain (ADR 0012), whose fixed secret opens none of the user's data, sealed under the Keychain's (ADR 0113). There alone do `PANE_AUTOMATION_DIR`, `PANE_DATA_DIR`, `PANE_UPDATE_*`, `PANE_TIME_SCALE`, `PANE_PROXY_TEST_URL`, `PANE_ELECTRON_DATA_DIR`, and `PANE_ENGINE_TRACE` count. Any process of the user's could start Pane's own binary with the automation channel, which read every profile, tab, and cookie of a launch without a PIN (verified), or point updates at a server of its own.
- **A launch that is not a scenario's passes Chromium no switch but Pane's own** (`pane/browser/browser_application.mm`): the URLs it opens and `--pane-wipe-after=`. `--remote-debugging-port`, `--enable-logging`, or `--load-extension` would have read every page once the PIN opened the data.
- **The hardened runtime, ad-hoc too** (`scripts/package.sh`): without it, a library injected with `DYLD_INSERT_LIBRARIES`, or a debugger, read the Keychain's secret and the open data with no prompt. Library validation and `restrict` stay off for ad-hoc code, which has no Team ID to match its framework by, and which dyld then refused to load from `@executable_path`. Measured: a library injected into the published 0.14.1 loaded into the browser and its twelve helpers; into this build, nowhere.
- **No Handoff** (patch 0002, `AppController updateHandoffManager:`): it advertised the shown tab's address and title to the user's other devices and to the system's activity records. The log showed `CREATED NSUserActivityTypeBrowsingWeb … BECOMECURRENT` for 0.14.1, and nothing for this build.
- **No crash handler** (patch 0008, `InitMacCrashReporter`): Crashpad's minidumps held the page's address (`url-chunk`), in its database under Chromium's default user data dir (`~/Library/Application Support/Chromium/Crashpad`), outside the volume and the panic wipe. macOS's own crash reports, which name no page, stay.
- **Downloads keep their quarantine and no address** (patch 0008, `quarantine_mac.mm`): Chromium wrote the file's and the page's URLs into `kMDItemWhereFroms`, which Spotlight indexes and Get Info shows, and into the quarantine's properties.
- **Local State keeps no time of use** (PaneKit `LocalState`, at launch and at quit, after Chromium writes it): each profile's `active_time` and the tab counts (`tab_stats`) go, and Default is named as used last.
- **No persistent histograms** (patch 0008): `BrowserMetrics`, 20 MB on the user's 0.15.1, kept counts of the pages loaded and the tabs open, one file per launch, for metrics Pane never sends. The patch skips their setup, and the spare file, in Pane's browser process. From 0.14.2 to 0.15.1, Pane turned them off with `--disable-features=PersistentHistograms`, which never reached them: `ChromeMainDelegate::SandboxInitialized` sets them up before the features are read.
- **The data root and its caches are the user's alone** (0700, `main.swift`): other accounts on the Mac listed them at 0755.
- **What earlier Panes left goes at launch:**
  - an earlier engine's histograms, with their spare and deferred files, beside the segmentation platform's database (`PanicWipe.stageEngineTraces`, for the panic wipe's erase);
  - 0.13's URL session caches: its update check's responses in `Cache.db` and `fsCachedData`, and its HTTP storage, under either identifier (`BundleIdentifier.removeSessionCaches`);
  - the former identifier's folders in the user's cache folder (`/var/folders/…/C/com.pane.desktop` and `.helper`), which held 0.13's update check and its helpers' shader caches, and the responses of scenario runs from before ADR 0114 (`BundleIdentifier.removeFormerUserCaches`);
  - in `migration.json`, the extensions each profile had: a record from before extensions stayed behind keeps them as `pendingExtensions`, which nothing reads, and the launch writes it again without them (`ElectronMigration.dropPendingExtensions`).

What the 0.15.1 audit confirmed:

- **The image is encrypted**: no SQLite header, URL, HTML, or cookie in its 1 GB of bands, at 7.997 bits a byte. `diskutil` reports its volume encrypted and locked, and refuses to mount it ("an encrypted and locked APFS Volume").
- **Every profile is in it**: each profile's folder, and its Chromium cache folder under `~/Library/Caches`, is a link into the volume. No profile is left as a plain folder.
- **The open data is sealed**: `profiles.json`, which names every tab, is a sealed box.
- **Nothing else outside the volume names a page**: Local State (no profile names, no accounts), the preferences (three window and panel keys), the 39 crash reports, the 513 quarantine events, and the system's cache folders for the identifier.
- **Chromium's Default profile stays plain at the engine's root, holding nothing of the user's**: Chromium opens it before the PIN. Its URLs are the 13 search engines of a Default made fresh, and its history and cookies are empty.

What stays, and whose call it is:

- **The PIN is optional** (the user's call, 2026-10-31): without one, the data key opens with the Keychain's secret alone.
- **A short PIN falls offline** to someone with the files and the Keychain's secret: a four-digit PIN in about 11 minutes. The PIN is four digits or more, as many as the user likes (the user's call), and its length is kept in `security.json`, so the unlock completes at the last digit. Only a key in the Secure Enclave stops it, which needs a Developer ID (ADR 0048); the user is getting one.
- **The sealed key and its copy** (`security.json`, `Data.security.json`), and the settings, which the user let stay readable (ADR 0113).
- **While Pane is open, the volume is mounted**, readable by the user's own processes, as any open app's data is. Pane locks on command, when the Mac sleeps or locks, and after a time without use, and the volume detaches behind the PIN screen (ADR 0116).
- **Site notifications** go through macOS, which shows and keeps their title and text (the user's call: as they are).
- **The GPU's shader caches** at the engine's root (`GraphiteDawnCache`) hold compiled GPU programs, not pages; turned off, every launch would compile them again.
- **macOS's own records**: when Pane was in use and which of its menu commands ran (Biome, knowledgeC), the quarantine events' times, and a print job's name.
- **Plain data from before 0.14** stays plain until the first unlock after the update, and the panic wipe's passes do not reach an SSD's old blocks: FileVault covers those. The Electron app's data stays where it was (ADR 0043).

What was rejected:

- **A build without the channel**: the shipped binary would not be the one the scenarios test, and the update check against the live release drives the published app.
- **Crashpad's database in the data root**: its dumps would still be plain between a crash and the next launch.
- **Downloads inside the volume**: the files are the user's, in their Downloads.

How it is checked:

- **`private-traces`**: after a download, a renderer's crash, and a quit, the download carries its quarantine and no `kMDItemWhereFroms`, no minidump exists, Local State names Default and keeps no time of use, and the data root is 0700. No histogram file exists at launch, while Pane runs, or after the quit. On 0.14.1 it failed at the download's attributes; on 0.15.1 it found `BrowserMetrics-6AE649C0-1746E.pma` at launch, and the record still listed its extensions.
- **PaneKit**: the launch stages an earlier engine's histograms and spare file for the erase, and leaves the GPU's caches. It removes the former identifier's cache folders and nothing else there. It writes a record again without its extensions, and leaves one without them as it was.
- **Once, on this Mac (2026-10-31)**: a library injected into this build and into 0.14.1, as above; the system log after a page loads, for Handoff; and the audit above, of the user's 0.15.1.
- The scenarios keep their hooks through the mock keychain, and the full run passes.
