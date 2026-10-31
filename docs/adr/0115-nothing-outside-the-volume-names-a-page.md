# Nothing outside the encrypted volume names a page, and only a scenario's launch takes Pane's test hooks

The user's requirement (2026-10-31): all of Pane's content is encrypted under the PIN, and none of it can be seen without it, whether by another process of the user's, by someone with a copy of the disk, or by someone at the Mac while Pane is locked. Four auditors, two on Claude Opus and two on Fable, each pair covering the data at rest and the runtime, read the code and ran throwaway scenarios against 0.14.1. What they found that Pane controls is closed here; what it does not is listed below, with whose call it is.

What changes:

- **Only a scenario's launch takes Pane's test hooks** (`app/Pane/Security/TestHooks.swift`). A scenario runs on the mock keychain (ADR 0013), whose fixed secret opens none of the user's data, sealed under the Keychain's (ADR 0113). There alone do `PANE_AUTOMATION_DIR`, `PANE_DATA_DIR`, `PANE_UPDATE_*`, `PANE_TIME_SCALE`, `PANE_PROXY_TEST_URL`, `PANE_ELECTRON_DATA_DIR`, and `PANE_ENGINE_TRACE` count. Any process of the user's could start Pane's own binary with the automation channel, which read every profile, tab, and cookie of a launch without a PIN (verified), or point updates at a server of its own.
- **A launch that is not a scenario's passes Chromium no switch but Pane's own** (`engine/pane/browser/browser_application.mm`): the URLs it opens and `--pane-wipe-after=`. `--remote-debugging-port`, `--enable-logging`, or `--load-extension` would have read every page once the PIN opened the data.
- **The hardened runtime, ad-hoc too** (`engine/scripts/package.sh`): without it, a library injected with `DYLD_INSERT_LIBRARIES`, or a debugger, read the Keychain's secret and the open data with no prompt. Library validation and `restrict` stay off for ad-hoc code, which has no Team ID to match its framework by, and which dyld then refused to load from `@executable_path`. Measured: a library injected into the published 0.14.1 loaded into the browser and its twelve helpers; into this build, nowhere.
- **No Handoff** (patch 0002, `AppController updateHandoffManager:`): it advertised the shown tab's address and title to the user's other devices and to the system's activity records. The log showed `CREATED NSUserActivityTypeBrowsingWeb … BECOMECURRENT` for 0.14.1, and nothing for this build.
- **No crash handler** (patch 0008, `InitMacCrashReporter`): Crashpad's minidumps held the page's address (`url-chunk`), in its database under Chromium's default user data dir (`~/Library/Application Support/Chromium/Crashpad`), outside the volume and the panic wipe. macOS's own crash reports, which name no page, stay.
- **Downloads keep their quarantine and no address** (patch 0008, `quarantine_mac.mm`): Chromium wrote the file's and the page's URLs into `kMDItemWhereFroms`, which Spotlight indexes and Get Info shows, and into the quarantine's properties.
- **Local State keeps no time of use** (PaneKit `LocalState`, at launch and now at quit, after Chromium writes it): each profile's `active_time` and the tab counts (`tab_stats`) go, and Default is named as used last.
- **No persistent histograms** (`PersistentHistograms` off): `BrowserMetrics` kept counts of the pages loaded and the tabs open, for metrics Pane never sends.
- **An earlier Pane's URL session caches go** (`BundleIdentifier.removeSessionCaches`): 0.13's update check kept its responses in `Cache.db` and `fsCachedData`, and its HTTP storage, under either identifier.
- **The data root and its caches are the user's alone** (0700, `main.swift`): other accounts on the Mac listed them at 0755.

What stays, and whose call it is:

- **The PIN is optional** (the user's call, 2026-10-31): without one, the data key opens with the Keychain's secret alone.
- **A short PIN falls offline** to someone with the files and the Keychain's secret: a four-digit PIN in about 11 minutes. The PIN is four digits or more, as many as the user likes (the user's call), and its length is kept in `security.json`, so the unlock completes at the last digit. Only a key in the Secure Enclave stops it, which needs a Developer ID (ADR 0048); the user is getting one.
- **Site notifications** go through macOS, which shows and keeps their title and text (the user's call: as they are).
- **Pane locks only at launch**: a Lock command, a lock when the Mac sleeps or locks, and one after an idle time are drawn in Paper (06.4, 07.9) and wait for the user's approval.
- **The GPU's shader caches** at the engine's root hold compiled shaders, not pages; turned off, every launch would compile them again.
- **macOS's own records**: when Pane was in use and which of its menu commands ran (Biome, knowledgeC), the quarantine events' times, and a print job's name.
- **Plain data from before 0.14** stays plain until the first unlock after the update, and the panic wipe's passes do not reach an SSD's old blocks: FileVault covers those. The Electron app's data stays where it was (ADR 0043).

What was rejected:

- **A build without the channel**: the shipped binary would not be the one the scenarios test, and the update check against the live release drives the published app.
- **Crashpad's database in the data root**: its dumps would still be plain between a crash and the next launch.
- **Downloads inside the volume**: the files are the user's, in their Downloads.

How it is checked:

- **`private-traces`**: after a download, a renderer's crash, and a quit, the download carries its quarantine and no `kMDItemWhereFroms`, no minidump exists, Local State names Default and keeps no time of use, and the data root is 0700. On 0.14.1 it failed at the download's attributes.
- **Once, on this Mac (2026-10-31)**: a library injected into this build and into 0.14.1, as above; the system log after a page loads, for Handoff.
- The scenarios keep their hooks through the mock keychain, and the full run passes on this build.
