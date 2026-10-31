# What the at-rest audit of 0.15.1 found, and what Pane erases at launch

The user asked (2026-10-31), once 0.15.1 had moved their data into the volume, to check that the cache and all of the browser's content, pages and cookies included, were encrypted. The audit read their installed copy's files for names, kinds, sizes, and counts, never their content, and tried the volume without the PIN.

What it confirmed:

- **The image is encrypted**: no SQLite header, URL, HTML, or cookie in its 1 GB of bands, at 7.997 bits a byte. `diskutil` reports its volume encrypted and locked, and refuses to mount it ("an encrypted and locked APFS Volume").
- **Every profile is in it**: each profile's folder, and its Chromium cache folder under `~/Library/Caches`, is a link into the volume. No profile is left as a plain folder.
- **The open data is sealed**: `profiles.json`, which names every tab, is a sealed box.
- **Nothing outside the volume names a page**: Local State (no profile names, no accounts), the preferences (three window and panel keys), the 39 crash reports, the 513 quarantine events, and the system's cache folders for the identifier.
- **Chromium's Default profile stays plain at the engine's root, holding nothing of the user's**: Chromium opens it before the PIN. Its URLs are the 13 search engines of a Default made fresh, and its history and cookies are empty.

What it found, and what changes:

- **The histograms were still written** (`BrowserMetrics`, 20 MB): counts of the pages loaded and the tabs open, one file per launch. ADR 0115 turned them off with `--disable-features=PersistentHistograms`, which never reached them: `ChromeMainDelegate::SandboxInitialized` sets them up before the features are read. Patch 0008 now skips that setup, and the spare file, in Pane's browser process. The launch erases what an earlier engine left, beside the segmentation platform's database (`PanicWipe.stageEngineTraces`).
- **`migration.json` listed the extensions each profile had**: a record from before extensions stayed behind keeps them as `pendingExtensions`, which nothing reads. The launch writes it again without them (`ElectronMigration.dropPendingExtensions`).
- **The former identifier's folders in the user's cache folder** (`/var/folders/…/C/com.pane.desktop` and `.helper`) held 0.13's update check and its helpers' shader caches, along with the responses of scenario runs from before ADR 0114. The launch removes them (`BundleIdentifier.removeFormerUserCaches`).

What stays, and why:

- **The GPU's shader caches** at the engine's root (`GraphiteDawnCache`) hold compiled GPU programs, not pages (ADR 0115).
- **The PIN's hash and the sealed keys** (`security.json`, `Data.security.json`), and the settings, which the user let stay readable.
- **While Pane is open, the volume is mounted**, readable by the user's own processes, as any open app's data is. Pane locks after a time without use, and when the Mac sleeps or locks (ADR 0116).

How it is checked:

- **`private-traces`**: no histogram file at launch, while Pane runs, or after the quit. On 0.15.1 it found `BrowserMetrics-6AE649C0-1746E.pma` at launch, and the record still listed its extensions.
- **PaneKit**: the launch stages an earlier engine's histograms and spare file for the erase, and leaves the GPU's caches. It removes the former identifier's cache folders and nothing else there. It writes a record again without its extensions, and leaves one without them as it was.
- **Once, on this Mac (2026-10-31)**: the audit above, of the user's 0.15.1.
