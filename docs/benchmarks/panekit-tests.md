# PaneKit's tests in pane_unittests

PaneKit, the Swift app's package for its model, rules, and data, held 287 tests in 28 files when it was deleted at the cutover to Views (ADR 0124; `docs/plans/views-migration.md`, K3). Each is listed here with the `pane_unittests` case that proves the same thing, with the same inputs and the same expectations, and the ones that can't have one, with why and what checks the behavior instead. The Swift tests are in git at `dce3d0f` (`app/PaneKit/Tests/PaneKitTests/`).

- **twin:** the C++ case or cases check every input and expectation of the Swift test, with the same values; plumbing may differ (temporary paths, a clock's API, ids that are objects), and a C++ case may check more.
- **twin, but:** as a twin, with a part the note names that the Views build no longer has, and why.
- **none:** no `pane_unittests` case can prove it, since the code it tested is gone from Pane's C++: the note says what replaced it and what checks it.

In all: 260 twins, 6 twins but for a part, and 21 with none, of 287. KeyRouting's overlays, which no scenario stacked at the cutover, are pressed as the app stacks them since K2a (`views-extensions`, `views-dialogs`, `views-lock`). Six cases were added or widened at the cutover where a twin had left an input or an expectation out: `FillsDefaults`, `PurgesDeletions` (new), `HostGPUClaim`, `SwiftVectorsTest.Vectors` and `CppWrites`, and `NewTabKey` (new).

## AddressInputTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `webURL`: keeps web URLs as they are | `pane/model/rules_unittest.cc` AddressInputTest.WebURL | twin | same two inputs; GURL writes the root path, so "http://example.com" is expected as "http://example.com/" (same address, still http, not searched); the first URL's string is identical |
| `bareHost`: loads bare hosts over https | `pane/model/rules_unittest.cc` AddressInputTest.BareHost | twin | same inputs; expected strings carry GURL's root path: "https://example.com/", "https://localhost:3000/" |
| `search`: searches anything else | `pane/model/rules_unittest.cc` AddressInputTest.Search | twin | same two queries and identical strings; C++ adds "c++ & rust" (escaped & and +) |
| `blank`: ignores blank input | `pane/model/rules_unittest.cc` AddressInputTest.Blank | twin | Swift's nil is an empty GURL: EXPECT_FALSE(AddressURL("   ").is_valid()) |
| `label`: a web address at rest is its domain and title; other text has no label | `pane/model/rules_unittest.cc` AddressInputTest.Label | twin | same four inputs and results; C++ compares the whole AddressLabel {domain, title} |

## BrowserModelTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `insertsAfterAnchor`: inserts a tab after its anchor, else at the end | `pane/model/browser_model_unittest.cc` BrowserModelTest.InsertsAfterAnchor | twin | same profile p [a,b,c], anchors a / none / "elsewhere" / profile "none", same expectations; C++ tabs x,y,z,w have an empty url by default |
| `removesToHistory`: removing a tab remembers it, forgets its use, and clears its selection | `pane/model/browser_model_unittest.cc` BrowserModelTest.RemovesToHistory | twin, but | all but the loaded mirror: the model keeps no page state in Views, whose closed tab's page goes with its WebContents (UiApp) |
| `removesProfile`: removing a profile forgets its tabs, collapses it, and clears its selection | `pane/model/browser_model_unittest.cc` BrowserModelTest.RemovesProfile | twin | same setup (expanded p, proxy status connected, select b then a) and the same 7 expectations; proxyStatus["p"] == nil is !proxy_status().contains("p") |
| `selects`: selecting a tab makes its profile active and it the most recent | `pane/model/browser_model_unittest.cc` BrowserModelTest.Selects | twin | same selections and expectations, SelectTab(std::nullopt) for select(tabID: nil) |
| `selectionObservers`: every change of the selection reaches each observer | `pane/model/browser_model_unittest.cc` BrowserModelTest.SelectionObservers | twin | same sequence and counts (2, 3, before+1, second == first); the two observeSelection closures are BrowserModel::Observers counting OnActiveTabChanged + OnActiveProfileChanged |
| `movesClamp`: moves clamp past either end, and ignore a source that is not there | `pane/model/browser_model_unittest.cc` BrowserModelTest.MovesClamp | twin | same moves (0->99, 2->-5, 7->0, profile "none"; profiles 0->10, 1->0, 3->0) and the same orders |
| `detectsChanges`: an edit reports what the engine must apply: its proxy, its fingerprint, its history choice | `pane/model/browser_model_unittest.cc` BrowserModelTest.DetectsChanges | twin | same 8 edits and change sets, proxy http proxy.example:8080; savesHistory = false is SetSavesHistory(false), .preset(.windows) is FingerprintPreset(Platform::kWindows) |
| `clearForgetsClosedTabs`: a clear forgets the profile's closed tabs, and no other's | `pane/model/browser_model_unittest.cc` BrowserModelTest.ClearForgetsClosedTabs | twin | same profiles a [one], b [two], same pops (profile b, then none) |
| `newProfileOwesNoClear`: a new profile, off from the start, has nothing to clear | `pane/model/browser_model_unittest.cc` BrowserModelTest.NewProfileOwesNoClear | twin | CreateProfile with SetSavesHistory(false): saves_history and clears_history both false, as the Swift |
| `timestamps`: profiles take the clock's time, created and updated | `pane/model/browser_model_unittest.cc` BrowserModelTest.Timestamps | twin | same clock (1_800_000_000 s, then +1.5 s), Work / rose / Clients, "2027-01-15T08:00:00.000Z" and "2027-01-15T08:00:01.500Z"; a SimpleTestClock for the ManualClock |
| `unloadClearsPageState`: an unloaded tab keeps nothing of its page | — | none | the model's mirrors of a page (loaded, loading, navigation, crashed, picture in picture, DevTools) were never ported: in Views they are the WebContents', which a sleep or an unload destroys (UiApp::Release); `sleep` checks the tab unloaded and kept |
| `debouncesSave`: a burst of changes saves once, after the delay, the profiles alone | `pane/model/browser_model_unittest.cc` BrowserModelTest.DebouncesSave | twin | same 3 changes, one pending save of kSaveDelay (300 ms in both), one save with titles ["Moved", ""] and no settings; the wait is the task environment's pending task (EachChangePutsTheSaveOff, :440, adds timing) |
| `savesSettings`: a settings change saves the settings alone, and only when they change | `pane/model/browser_model_unittest.cc` BrowserModelTest.SavesSettings | twin | default theme schedules nothing, dark saves settings alone (theme dark, no profiles) |
| `unchangedTabSavesNothing`: a page reporting what the tab already holds saves nothing | `pane/model/browser_model_unittest.cc` BrowserModelTest.UnchangedTabSavesNothing | twin | title "A" over "A": nothing waits |
| `savesNow`: saveNow writes what changed at once, and nothing when nothing did | `pane/model/browser_model_unittest.cc` BrowserModelTest.SavesNow | twin | same steps: nothing, expansion only, then New/blue/no group + sidebar 300: one save of 2 profiles and width 300, no wait left, Fire saves no more |
| `retriesFailedWrite`: a failed write asks again, and the next save writes that file | `pane/model/browser_model_unittest.cc` BrowserModelTest.RetriesFailedWrite | twin | like the Swift, ScheduleSave({kProfiles}) then one save of ["p"]; the failed write's retry itself is ModelStoreTest.RetriesAFailedWrite (model_store_unittest.cc) |

## ClientHintsTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `staleVersion`: rewrites a stale claimed Chrome version to the real engine's | `pane/model/client_hints_unittest.cc` ReconcileChromeVersionTest.StaleVersion | twin | same claimed Windows UA (Chrome/136.0.0.0), real UA (Chrome/146.0.7680.65) and expected string |
| `restIntact`: leaves the rest of the claimed UA (platform token, Safari suffix) intact | `pane/model/client_hints_unittest.cc` ReconcileChromeVersionTest.RestIntact | twin | same claimed X11 Linux UA and expected string |
| `matchingVersion`: is a no-op when the claimed version already matches the engine | `pane/model/client_hints_unittest.cc` ReconcileChromeVersionTest.MatchingVersion | twin | same: the real UA reconciled with itself is unchanged |
| `noChromeToken`: returns the claimed UA unchanged when the real UA carries no Chrome token | `pane/model/client_hints_unittest.cc` ReconcileChromeVersionTest.NoChromeToken | twin | same claimed UA and real UA "Mozilla/5.0 (weird runtime)" |
| `bareMajor`: replaces a bare major version too | `pane/model/client_hints_unittest.cc` ReconcileChromeVersionTest.BareMajor | twin | same: "Mozilla/5.0 Chrome/136 Safari/537.36" becomes "Mozilla/5.0 Chrome/146.0.7680.65 Safari/537.36" |
| `chromiumBrandList`: the brand list matches Chromium's for an unbranded build | `pane/model/client_hints_unittest.cc` ClientHintsTest.ChromiumBrandList | twin | same four BrandList calls (seeds 84 and 85, no brand, "84"/"84.0.0.0"/"85"/"85.0.0.0") and serialized lists |
| `googleChromeBrands`: Sec-CH-UA matches Google Chrome's | `pane/model/client_hints_unittest.cc` ClientHintsTest.GoogleChromeBrands | twin | the four @Test arguments (120, 124, 131, 136) and their Sec-CH-UA strings are a loop in one C++ case; same UA "Mozilla/5.0 Chrome/<major>.0.0.0 Safari/537.36" and engine "<major>.0.6099.71" |
| `fullVersion`: the full version list carries the engine's full version | `pane/model/client_hints_unittest.cc` ClientHintsTest.FullVersion | twin | same: full version 156.0.8078.48, full version list and brands strings |
| `otherMajor`: a claim for another major keeps its own full version | `pane/model/client_hints_unittest.cc` ClientHintsTest.OtherMajor | twin | same: full version 136.0.7103.92 and brand Google Chrome 136 (HasBrand for contains) |
| `noChromeToken`: a UA without a Chrome token falls back to Chrome 136 | `pane/model/client_hints_unittest.cc` ClientHintsTest.NoChromeToken | twin | same: full version 136.0.0.0 and brand Chromium 136 |
| `platforms`: platform and platform version follow the claimed OS; Linux reports none | `pane/model/client_hints_unittest.cc` ClientHintsTest.Platforms | twin | same: Windows/15.0.0, macOS/14.6.1, Linux/""; each tuple compare is two EXPECT_EQs |
| `architecture`: the architecture is arm for an Apple Silicon GPU, x86 otherwise | `pane/model/client_hints_unittest.cc` ClientHintsTest.Architecture | twin | same Apple M1 and Intel Iris renderers, arm and x86 |
| `headers`: the headers serialize as Chrome writes them | `pane/model/client_hints_unittest.cc` ClientHintsTest.Headers | twin | same nine header values through the same header oracle |
| `userAgent`: the User-Agent and its metadata follow the engine's version | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.UserAgent | twin | same profile "profile-a", engine UA and version 156.0.8078.48, and all five expectations (UA, full version, Windows, ["Desktop"], Win32) |
| `screen`: the available screen leaves room for each OS's bars | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.Screen | twin | same screens (1920x1080x24, 1440x900x30) and expected avail sizes; Linux availHeight 1053 |
| `deviceMemory`: device memory is one Chrome reports: a power of two from 0.25 to 8 | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.DeviceMemory | twin | same five values: 8->8, 16->8, 6->4, 1->1, 0.1->0.25 |
| `noiseSeed`: the noise seed is the Electron app's hash of the profile ID | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.NoiseSeed | twin | same five profile ids and seeds |
| `platformDerived`: pixel ratio and voices follow the claimed platform, on a Mac host | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.PlatformDerived | twin | same DPR 1 and 2 and "MacIntel"; voices checked as platform != HostPlatform() over Windows, macOS and Linux, which on the Mac (kMacOS) is the Swift's (Windows hides, macOS shows) plus Linux hides |
| `languagesAndTimezone`: languages fall back to the language, and an empty timezone to none | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.LanguagesAndTimezone | twin | same: languages ["pt-BR"], locale pt-BR, no timezone, "Linux x86_64" |
| `json`: the JSON is stable and decodes back | `pane/model/client_hints_unittest.cc` EngineFingerprintTest.Json | twin | same: stable ToJson, same prefix, FromJson(text) == engine (the Swift JSONDecoder round trip) |

## CrashLoopTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `secondWithinMinute`: a first crash reloads; a second within a minute is a loop | `pane/model/rules_unittest.cc` CrashLoopTest.SecondWithinMinute | twin | same start (1_800_000_000 s), crashes a@0, a@59, b@59 -> false, true, false; Loops() mirrors the Swift helper |
| `afterMinute`: a crash a minute or more after the last reloads again | `pane/model/rules_unittest.cc` CrashLoopTest.AfterMinute | twin | same crashes a@0, a@60, a@61 -> false, false, true |
| `forgets`: a forgotten tab starts over | `pane/model/rules_unittest.cc` CrashLoopTest.Forgets | twin | same: a@0, a@1, forgetting "a" after crash 0 -> false, false |

## DataKeyTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `volumePassphraseMovesOn`: volumePassphraseMovesOn | `pane/model/data_key_unittest.cc` DataKeyTest.VolumePassphraseMovesOn | twin | Same key (32 bytes of 7); the former passphrase checked against HKDF-SHA256 with no salt, info "Pane data volume", 32 bytes, base64, as in Swift. |
| `engineKeyIsPerProfile`: engineKeyIsPerProfile | `pane/model/data_key_unittest.cc` DataKeyTest.EngineKeyIsPerProfile | twin | Same ids (...9f02, ...9f03) and hex vector 095fb86a...4fc2; Swift's DataKey.engineKey(_:profile:) is EngineFilesKey(). |
| `withoutPin`: sealed without a PIN, the key opens with the secret alone | `pane/model/data_key_unittest.cc` DataKeyTest.WithoutPin | twin | Same secret "engine-safe-storage-secret", "another" and PIN "1234"; Swift's thrown DataKey.OtherSecret is OpenError::kOtherSecret. |
| `withPin`: sealed with a PIN, the key opens with the secret and that PIN only | `pane/model/data_key_unittest.cc` DataKeyTest.WithPin | twin | Same PINs 908172/908173; Swift's 'throws, but not OtherSecret' is checked as OpenError::kRefused, which is stricter. |
| `reseal`: a new PIN reseals the same key, which the old PIN no longer opens | `pane/model/data_key_unittest.cc` DataKeyTest.Reseal | twin | Same PINs 1234 then 5678: salts differ, the new PIN opens the same key, the old one fails. |
| `tampered`: a box changed since it was sealed does not open | `pane/model/data_key_unittest.cc` DataKeyTest.Tampered | twin | Same flip of the box's last byte (^= 0x01); C++ edits the sealed copy's box where Swift built a new Sealed with pin nil and the same secret. |
| `derived`: each use has a key of its own, the same for the same data key | `pane/model/data_key_unittest.cc` DataKeyTest.Derived | twin | All five checks: files key stable and not the data key, passphrase stable and per key, base64-decoded passphrase not the files key. |
| `roundTrip`: writes nothing in the clear, and reads back with its key only | `pane/model/data_key_unittest.cc` SealedFileTest.RoundTrip | twin | Same {"inbox": "https://mail.example.com/inbox"} as "profiles"; another key's load is checked as FileError::kRefused (Swift: any error). |
| `moved`: a box moved to another file does not open | `pane/model/data_key_unittest.cc` SealedFileTest.Moved | twin | Same: profiles' box copied to "deletions" fails to load under the same key. |
| `plain`: a plain file, as PersistedFile writes it, does not read | `pane/model/data_key_unittest.cc` SealedFileTest.Plain | twin | Same: a PersistedFile "profiles" fails to load as a SealedFile. |
| `missing`: a file not written yet reads as nil | `pane/model/data_key_unittest.cc` SealedFileTest.Missing | twin | Same: a missing "profiles" loads as nothing (value nullopt, not an error). |

## DataVolumeTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `encryptedMarker`: a profile's files count as encrypted once marked, beside them in the volume | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.EncryptedMarker | twin | Same profile "p" and "other", no cache directory. |
| `safetyCopyGoes`: a safety copy goes from the third open that comes up, once the repair that reads it has run | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.SafetyCopyGoes | twin | Same four opens, then repaired: SafetyCopy and .safety-opened-3 gone, FilesAreRepaired true. |
| `nothingToRepair`: a profile this Pane encrypted has nothing to repair | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.NothingToRepair | twin | Same. |
| `noSafetyCopy`: no safety copy, nothing to count | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.NoSafetyCopy | twin | Same three kNone. |
| `safetyCopyRestores`: two opens killed before any came up restore the copy, once | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.SafetyCopyRestores | twin | Same none, none, restore; not encrypted after the restore; three kNone after; the copy stays. |
| `safetyCopyAfterAGoodOpen`: a profile that came up once never goes back to its copy, however many opens are killed after | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.SafetyCopyAfterAGoodOpen | twin | Same one good open then five kNone. |
| `safetyCopyAfterAQuit`: a quit or a lock inside an open is no failure: only a kill counts | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.SafetyCopyAfterAQuit | twin | Same five opens each closed by SafetyCopiesClosed, then none, none, restore. |
| `safetyCopyFromAnEarlierPane`: what 0.18.0's opens left counts for nothing: it counted quits as kills | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.SafetyCopyFromAnEarlierPane | twin | Same .safety-opening and .safety-failed markers (empty), removed by the first open; then none, restore. |
| `hasFolder`: a profile has a folder only once it has been prepared | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.HasFolder | twin | Same. |
| `openTwice`: a volume a lock could not detach opens as it is | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.OpenTwice; `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.OpenTwiceWithHdiutil | twin | Same real image, passphrase "pass", opened twice, mounted each time; C++ also runs it through hdiutil (not in PaneKit), and its teardown detaches only its own image. |
| `formerPassphraseThatWillNotChange`: a passphrase that will not change stops the open: nothing converts while the former one still opens the volume | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.FormerPassphraseThatWillNotChange; `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.FormerPassphraseThatWillNotChangeWithHdiutil | twin | Same image made with "former", Open(devices, "", "former") fails, not mounted; Swift's DataVolume.Failure is VolumeError::Kind::kTool; plus an hdiutil variant. |
| `prepare`: a profile's engine folder and caches are links into the volume | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.Prepare | twin | Same: prepared twice, Engine/p and caches' Engine/p link to the folder's Profile and Cache, Cookies lands inside, not mounted. |
| `downloadCache`: a profile that saves no history opens without its earlier downloads' log or caches | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.DownloadCache | twin | Same Profile/shared_proto_db, Profile/Cache, Profile/Code Cache, Cache/Cache, Cache/Code Cache kept with history, removed without; Profile/Local Storage kept. |
| `relink`: a link to somewhere else is replaced | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.Relink | twin | Same link to root/elsewhere replaced by the volume's Profile. |
| `moveIn`: an earlier Pane's plain folders move in, whole, and wait for the wipe's erase | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.MoveIn | twin | Same files (300000 bytes of 7, Cookies, SingletonCookie->12345, Cache/entry), [p, never-opened], shares, passes [1, 3]; PanicWipe.staged/eraseStaged are StagedWipes/EraseStagedWipes. |
| `moveAgain`: a move cut short before its stage copies the folder again | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.MoveAgain | twin | Same "whole" Cookies copied again, "half" Partial gone. |
| `engineTraces`: an earlier engine's database of the pages visited, and its histograms, are staged for the erase | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.EngineTraces | twin | Same traces and GraphiteDawnCache; PanicWipe.stageEngineTraces/staged/eraseStaged are StageEngineTraces/StagedWipes/EraseStagedWipes (pane/model/mac/panic_wipe.h). |
| `wipe`: the wipe erases the volume's image without passes, and the key's copy before security.json | `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.Wipe | twin | Same /tmp/pane, no caches: contains Data.sparsebundle, ends with Data.security.json, security.json; PanicWipe.targets is WipeTargets. |
| `startsOnDefault`: the engine starts on its Default profile, with no profile of Pane's open last, and keeps no time of use | `pane/model/persistence_unittest.cc` LocalStateTest.StartsOnDefault | twin | Same Local State JSON and all nine checks; C++ StartingOnDefaultProfile takes and returns a parsed base::Value where Swift took and returned Data. |
| `moveFormerData`: an earlier Pane's data and caches move to this identifier's folders, once | `pane/model/mac/bundle_identifier_unittest.cc` BundleIdentifierTest.MoveFormerData | twin | Same folders, profiles.json "{}", preferences ("0 0 1280 800", NSOSPLastRootDirectory) and second move; the domain is named for CFPreferences, not a UserDefaults. |
| `formerUserCaches`: the former identifier's folders in the user's cache folder go, and nothing else there | `pane/model/mac/bundle_identifier_unittest.cc` SessionCachesTest.FormerUserCaches | twin | Same four folders, [pane-new, pane-new.helper] left, second call empty, the user cache folder named "C". |
| `removed`: an earlier Pane's URL session caches go, under either identifier, and nothing else of its folders | `pane/model/mac/bundle_identifier_unittest.cc` SessionCachesTest.Removed | twin | Same pane-a and pane-b: Cache.db, fsCachedData, HTTPStorages gone, Engine kept. |
| `held`: the data's lock is one Pane's while it runs, and the next one's once it lets go | `pane/model/mac/data_lock_unittest.cc` DataLockTest.Held | twin | Same: one holder, a second Take refused, Wait(3 s) after the release. |
| `waits`: a Pane started while another quits takes the lock once that one lets go | `pane/model/mac/data_lock_unittest.cc` DataLockTest.Waits | twin | Same 200 ms, Wait(30 s) on another thread (base::Thread for Swift's detached Task), then Wait(0.1 s) refused. |
| `notInherited`: a process Pane starts does not keep the lock: the update's relaunch takes it | `pane/model/mac/data_lock_unittest.cc` DataLockTest.NotInherited | twin | Same /bin/sleep 5 child, Wait(2 s) after the release. |

## ElectronMigrationTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `migrates`: moves profiles, settings, the PIN, and each profile's data; never touches the source | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.Migrates | twin | Same Electron fixture and every check: migrated(1), source snapshot unchanged, Work/keepLoaded/tab URL, theme dark, unload nil, PIN length 4, copied vs skipped files, record profiles [id], second run notNeeded |
| `preconditions`: waits while the Electron app runs, and does nothing without it | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.Preconditions | twin | Same codesigned sleep copy in Pane.app with Electron Framework.framework, SingletonLock host-<pid>: sourceInUse, no profiles.json written, noSource for <source>/missing |
| `staleLock`: a stale lock whose pid is now another process does not hold it back | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.StaleLock | twin | Same SingletonLock naming the test process own pid (GetCurrentProcId for getpid): migrated(1) |
| `notCommitted`: a launch that made nothing, or one cut short, still migrates | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.NotCommitted | twin | Same empty profiles/settings/security pre-saved: migrated(1), theme dark, PIN length 4, profiles [Work] (C++ checks size 1 and name Work) |
| `recordCookieKey`: a record keeps whether the cookie key still waits; an older one reads as not | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.RecordCookieKey | twin | Same record (/old, 2026-10-27T00:00:00Z, [a], pending true) and same older JSON (2026-10-26, pendingExtensions {}) reading as nullopt |
| `defaultSource`: reads the Electron app's folder used last, named after its package | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.DefaultSource | twin | Same: no folder gives veil and @pane/veil Safe Storage; mtimes 2026-05-01 and 2026-05-05 UTC (TouchFile) give @pane/desktop, its directory and service; a bare directory has no service |
| `existingData`: the native app's own profiles are never replaced | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.ExistingData | twin | Same own profile (own, Mine, 2026-10-27T00:00:00Z) then an unreadable "{" profiles.json: notNeeded both times |
| `dropsPendingExtensions`: a record from before extensions stayed behind is written again without them | `pane/model/mac/electron_migration_unittest.cc` ElectronMigrationTest.DropsPendingExtensions | twin | Same old record JSON; pendingExtensions and cjpalhdlnbpafiamejdnhcphjbkeiagm gone, profiles [a] and source /x/@pane/veil kept, a second run leaves the bytes unchanged |

## ExtensionPromptTextTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `addingNamesPane`: adding names Pane, with the name's quotes curled | `pane/model/rules_unittest.cc` ExtensionPromptTextTest.AddingNamesPane | twin | same inputs and outputs (ExtensionPromptTitle, ExtensionPromptAcceptTitle in pane/model/extension_text.h); the C++ literal curly quotes are U+201C/U+201D, as the Swift's \u{201C}/\u{201D} |
| `otherPrompts`: Chrome's other prompts keep their title and button, quotes curled | `pane/model/rules_unittest.cc` ExtensionPromptTextTest.OtherPrompts | twin | same inputs and outputs ("Accept permissions"/"uBlock Origin Lite" title curled; "Re-enable" kept) |
| `curlyQuotes`: curls only the first and last straight quotes, and only a pair | `pane/model/rules_unittest.cc` ExtensionPromptTextTest.CurlyQuotes | twin | same three cases (CurlyQuoted) |
| `removing`: removing asks with the name, and reports abuse on the Web Store | `pane/model/rules_unittest.cc` ExtensionPromptTextTest.Removing | twin | same; the Swift compares URL?.absoluteString, the C++ GURL::spec(), to the same string |

## ExtensionShortcutTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `glyphs`: Chrome's shortcut reads in the Mac's order, the key last | — | none | Pane's shortcuts dialog is gone: Chrome's own shortcuts page writes the keys (D3, ADR 0098) |
| `heldModifiers`: held modifiers read in the same order | — | none | Pane's shortcuts dialog is gone: Chrome's page records the keys (D3) |
| `includeModifier`: a key alone needs Ctrl, Alt, or ⌘; Shift is not one | — | none | Chrome's page refuses a key without ⌃, ⌥, or ⌘ itself (D3) |
| `invalidCombination`: ⌥ with ⌃ or ⌘, or three modifiers, is an invalid combination | — | none | Chrome's page refuses ⌥ with ⌃ or ⌘ itself (D3) |
| `messages`: the messages are Chrome's macOS strings, and Pane's for its own keys | — | none | the refusals' words are Chrome's page's; Pane's refusal of its menu bar's keys has no message (Paper 10.3), and `extension-shortcuts` checks it (ShortcutRefusal) |

## ExtensionSiteTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `title`: a site reads without the list's path, and without its scheme when any | — | none | Pane's Details view is gone: Chrome's Details page lists an extension's sites (D3, ADR 0062) |

## KeyRoutingTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `nothingShows`: with nothing over the window, every key goes on | — | none | no router in Views (ADR 0079): with nothing over the window, a key goes to the focused view or the page, and the command table's to the window's FocusManager |
| `newTab`: ⌘T is reserved, Caps Lock or not; ⇧⌘T and a typed t are not | `pane/ui/app/commands_unittest.cc` CommandsTest.NewTabKey; `pane/ui/app/commands_unittest.cc` CommandsTest.AppFirstAsChromeReserves | twin | NewTabKey, added at the cutover: ⌘T with and without Caps Lock, ⇧⌘T, a typed t. Views reserves ⇧⌘T ahead of the page too, as Chrome does |
| `pinScreen`: the PIN screen takes every key but Quit | — | none | the PIN screen holds the keys itself (LockScreenView), and the frame lets only Quit through (frame_mac.mm); `views-lock` types digits, Delete, and Esc into it as keys, and `security` Return |
| `quitWithCapsLock`: ⌘Q gets through with Caps Lock on, and only ⌘Q | `pane/ui/app/commands_unittest.cc` CommandsTest.EachKeyRunsOneCommand | twin, but | ⌘Q with Caps Lock is Quit in the command table; the PIN screen letting it through is the window's frame (frame_mac.mm), checked by `security` with the front |
| `quitConfirmation`: Quit Pane? goes back on Esc, quits on Return, and takes the rest | — | none | Quit Pane? is a dialog that takes every key (ModalDialog), ⌘Q passing through the frame; `quit` checks its Esc and its Return |
| `appIconPicker`: the App icon picker closes on Esc and Return and lets the rest reach its icons | — | none | the picker is a dialog that takes Esc and Return (ModalDialog); Quit Pane? over it takes Esc; `app-icon` checks its Esc and Return, and `key-precedence` Esc with Quit Pane? over it |
| `extensionShortcuts`: Keyboard shortcuts closes on Esc and Return; a field recording takes every key | — | none | Pane's shortcuts dialog is gone (D3): Chrome's page records a shortcut, and has every key first while it does (Window::PreHandleKeyboardEvent); `extension-shortcuts` checks the page's keys |
| `addingSite`: Add a site cancels on Esc and adds on Return; the rest is typed into its field | — | none | Pane's Add a site field is gone (D3): Chrome's Details page adds a site in its own dialog; `extension-details` drives Chrome's Details page |
| `quitOverPrompt`: Esc goes to the topmost overlay: Quit Pane? over an extension's popup | — | none | Quit Pane? over an extension's prompt takes Esc first, the prompt standing aside (RootView::OverlayKey), and the prompt's Esc is next, cancelling it: `views-extensions` presses both, through Views' own dispatch in the background and as posted keys with `--foreground` |
| `pinOverQuit`: the PIN screen over Quit Pane? | — | none | Views never stacks them: a Lock dismisses Quit Pane?, as it does the switcher (UiApp::LockNow), where the Swift drew the PIN screen over it; the PIN screen alone has the keys, and Quit Pane? doesn't come back after the PIN. `views-lock` presses Esc at that PIN screen and types the PIN by its keys (lockOverQuit), which found the PIN screen without the keys after a Lock over Quit Pane? (fixed at the cutover: RootView::ReturnKeys) |
| `extensionPrompt`: an extension's popup takes all but shortcuts | — | none | the prompt takes Esc and Return ahead of the page (RootView::OverlayKey) and swallows the rest while it shows; `views-extensions` presses Esc (cancels) and Return (accepts) in the add prompt, which Chrome asks for an extension loaded already (the channel's `askToAddExtension`), and in the remove prompt (Esc keeps the extension, Return removes it), in the background and with `--foreground` |
| `promptOverMenu`: the popup over the extensions menu, the menu over the switcher | — | none | the app never stacks them: a prompt closes the extensions menu as it comes, in Views (ExtensionsUi::Ask) as in the Swift (`present`'s `closeMenu`), so Esc answers the prompt alone; the switcher is drawn over the menu and over a prompt, and takes Esc first (Control held), where the Swift's menu, a window of its own, was over it. `views-extensions` checks each |
| `extensionsMenu`: Esc closes the more menu, then the menu; the rest goes on | — | none | Esc closes the more menu, then the menu (RootView::OverlayKey, ExtensionsUi::Escape); Return and other keys go on; `extensions-menu` checks the menu's Esc, installing from the live Chrome Web Store, and `views-extensions` the more menu's then the menu's, with no store |
| `switcher`: Ctrl+Tab opens the switcher and steps through it; Control's release picks | `pane/ui/app/commands_unittest.cc` CommandsTest.EachKeyRunsOneCommand; `pane/ui/app/commands_unittest.cc` CommandsTest.AppFirstAsChromeReserves | twin, but | ⌃⇥ and ⌃⇧⇥ run the switcher ahead of the page; cycling, Esc, and Control's release are the window's (Window::SwitcherEscape, ControlReleased), checked by `shortcuts`: ⌃⇥ and Control's release, ⌃⇧⇥, and Esc |
| `dialog`: a page's dialog: Esc dismisses, Return confirms, a field keeps its typing | — | none | a page's dialog takes Esc and Return ahead of the page (RootView::OverlayKey, PageDialog), and the rest unless a field types; `dialogs` checks its Return and Esc |
| `dialogOrder`: the switcher over a dialog, a dialog over a page in fullscreen | — | none | the switcher over a page's dialog takes Esc first, Control held, and the dialog's Esc is next (`views-dialogs`); a dialog over a page's fullscreen has Esc before the fullscreen's exit since the cutover (HostedBrowserWindow::PreHandleKeyboardEvent), which `fullscreen` checks with the front |
| `pageFullscreen`: Esc leaves a page's fullscreen, unless the switcher shows | — | none | Esc leaves a page's fullscreen through Chrome's ExclusiveAccessManager, after Pane's overlays, the switcher's first, since the cutover (HostedBrowserWindow::PreHandleKeyboardEvent); `fullscreen` checks Esc with the front |

## MostRecentTabTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `mostRecentPresent`: returns the most recently used tab still present | `pane/model/rules_unittest.cc` MostRecentTabTest.MostRecentPresent | twin | C++ takes tab ids (strings) where Swift took Identifiable items; same tabs a,b,c, history [x,b,c,a] -> b |
| `skipsGone`: skips history ids that are no longer present | `pane/model/rules_unittest.cc` MostRecentTabTest.SkipsGone | twin | ids instead of items; same history [gone,c,a] -> c |
| `fallsBackToLast`: falls back to the last tab when none are in the history | `pane/model/rules_unittest.cc` MostRecentTabTest.FallsBackToLast | twin | ids instead of items; same histories [] and [nope] -> c |
| `emptyTabs`: returns nil when there are no tabs | `pane/model/rules_unittest.cc` MostRecentTabTest.EmptyTabs | twin | no tabs, history [a] -> std::nullopt (Swift nil) |

## PersistenceTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `fillsDefaults`: fills defaults for fields a persisted settings.json predates | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.FillsDefaults | twin | the Swift's defaults spelled out at the cutover (theme system, 15, 30, 300, and the rest) |
| `restoresTheme`: restores a persisted theme | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.RestoresTheme | twin | same JSON {"chromiumPath":"","theme":"dark"}, theme dark |
| `restoresAppIcon`: restores a persisted app icon, automatic by default | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.RestoresAppIcon | twin | default automatic, JSON "dusk" reads dusk, a saved moonrise reads back |
| `automaticAppIcon`: shows Day under a light appearance and Night under a dark one when automatic | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.AutomaticAppIcon | twin | same 4 cases; the loop kDay..kMoonrise is AppIcon.allCases minus automatic (same 10 icons, same order) |
| `lockAfter`: locks after 15 minutes without use by default, and keeps Never | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.LockAfter | twin | pre-ADR-0116 JSON {"theme":"dark"} reads 15; saved nil and 1 read back |
| `explicitNullIsOff`: keeps an explicit null as off | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.ExplicitNullIsOff | twin | same JSON tabSleepAfterMinutes null: nil, profileUnloadAfterMinutes 30 |
| `sidebarWidth`: keeps the sidebar's width, within its range | `pane/model/persistence_unittest.cc` SettingsPersistenceTest.SidebarWidth | twin | default 260, 312 round-trips, JSON 900 reads 400, ClampedSidebarWidth(120) == 200 |
| `readsElectronFile`: reads the Electron app's profiles.json with its defaults | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.ReadsElectronFile | twin | the Electron profiles.json literal is byte-identical; same 11 expectations (Tab favicon "" and ProxyConfig password nullopt by default) |
| `roundTrips`: round-trips through save and load | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.RoundTrips | twin | same profile p1 Work violet Clients keepLoaded true savesHistory false, tab t1 https://example.com/, a/b; load equals what was saved |
| `chosenTimeZone`: a zone chosen outlives a quit, and an older Pane still finds the key it requires | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.ChosenTimeZone | twin | macOS preset with Asia/Tokyo reads back; the file holds both "timeZone":"Asia\/Tokyo" and "timezone":"Asia\/Tokyo" |
| `clearOwedPersists`: a clear still owed outlives a quit | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.ClearOwedPersists | twin | SetSavesHistory(false) for savesHistory = false; clears_history true after the round trip |
| `olderFileOwesAClear`: a file from before the owed clear's key owes one, on a profile that is off | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.OlderFileOwesAClear | twin | same 4 cases (false,nil)->true, (true,nil)->false, (false,false)->false, (false,true)->true, historyClearOwed removed for nil; ToValue/FromValue for JSONEncoder/Decoder |
| `writesNulls`: writes explicit nulls, as the Electron app does | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.WritesNulls | twin | same 4 substrings "group":null, "proxy":null, "fingerprint":null, "version":0 |
| `missingFile`: reports a missing file as nil | `pane/model/persistence_unittest.cc` ProfilesPersistenceTest.MissingFile | twin | Load() of a missing profiles file is nullopt |
| `use`: moves a used tab to the front once | `pane/model/rules_unittest.cc` TabHistoryTest.Use | twin | use a, b, a: mru [a, b] |
| `forget`: forgets a closed tab | `pane/model/rules_unittest.cc` TabHistoryTest.Forget | twin | use a, b, forget a: mru [b] |
| `closedStack`: keeps the 20 most recently closed tabs, newest first | `pane/model/rules_unittest.cc` TabHistoryTest.ClosedStack | twin | 25 pushes of https://<n>.test/ in p: 20 kept, pop gives https://24.test/, 19 left |
| `emptyPop`: pops nothing from an empty stack | `pane/model/rules_unittest.cc` TabHistoryTest.EmptyPop | twin | PopClosed() on an empty history is nullopt |

## ProfileDeletionsTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `purge`: a delete the engine never confirmed is finished at the next launch | `pane/model/persistence_unittest.cc` ProfileDeletionsTest.Purge; `pane/model/mac/data_volume_unittest.cc` DataVolumeTest.PurgesDeletions | twin | PurgesDeletions, added at the cutover, runs the purge over the volume's own Prepare and Remove, as the Swift's did |

## ProfileDraftTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `newForm`: a new profile's form: spoofing on the host's platform, no proxy | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.NewForm | twin | same six checks; Swift compares with the constant ProfileDraft.hostPlatform (.macOS), C++ with HostPlatform() (kMacOS on the Mac) |
| `untouchedEdit`: saving an edit untouched leaves the profile as it was | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.UntouchedEdit | twin | same profile (windows preset, socks5 proxy.example:1080 user/"p:ss"), same failure if the fingerprint is regenerated, same saved == original |
| `fillsForm`: an existing profile fills the form | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.FillsForm; `pane/model/profile_draft_unittest.cc` ProfileDraftTest.GroupKept | twin, but | all but the group: the sheet has no Group field (the user's answer, N1), and GroupKept proves a saved group stays as it is |
| `timeZone`: the time zone goes from the profile to the form and back, and survives another platform | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.TimeZone | twin | same steps: Asia/Tokyo kept across macOS->windows, emptied to "", spoofing off -> no fingerprint and an empty zone |
| `newTimeZone`: a new profile claims no time zone: it follows the computer | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.NewTimeZone | twin | Platform.allCases is windows, macOS, linux: the C++ loops over the same three |
| `clearOwed`: turning history off owes a clear, kept until it is done; back on owes none | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.ClearOwed | twin | same sequence; the Swift savesHistory setter is BrowserProfile::SetSavesHistory in C++ |
| `historyChoice`: the history choice goes from the profile to the form and back | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.HistoryChoice | twin | same |
| `ipv6Host`: saving an IPv6 host leaves it and the credentials as they are | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.Ipv6Host | twin | same host [2606:4700:4700::1111], port 1080, user; paste refused, fields unchanged |
| `nameRequired`: a name is required, spaces alone are none | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.NameRequired | twin | same |
| `portChecked`: a proxy with a host saves only with a port of 1 to 65535 | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.PortChecked | twin | same five bad ports ("0", "65536", "-1", "abc", "") each kPortInvalid with no proxy; " 65535 " valid with port 65535 |
| `nameFirst`: a missing name is the first problem, before the port | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.NameFirst | twin | same |
| `noProxy`: the proxy off saves as no proxy, whatever its fields hold | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.NoProxy | twin | same |
| `hostMissing`: the proxy on does not save without a host, before anything about its port | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.HostMissing | twin | same |
| `hostInvalid`: the proxy on does not save with a host Chrome cannot read | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.HostInvalid | twin | all 12 invalid and 10 valid hosts, same verdicts ("exämple.test" is U+00E4 as in the Swift); C++ adds 4 invalid hosts: "00001::", "::1.2.3.08", "010.1.1.1", "::ffff:010.1.1.1" |
| `proxyFields`: the proxy trims its host and leaves empty credentials out | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.ProxyFields | twin | same |
| `paste`: a pasted proxy fills its fields; one it leaves out keeps what was typed | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.Paste | twin | same three pastes ("proxy.example:3128", "host:8080:user:p:ss", "host:70000") and the same field checks |
| `newFingerprint`: a new profile gets a generated fingerprint while spoofing, none without | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.NewFingerprint | twin | same |
| `editedFingerprint`: changing the platform regenerates the fingerprint; a group of spaces is none | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.EditedFingerprint; `pane/model/profile_draft_unittest.cc` ProfileDraftTest.GroupKept | twin, but | the fingerprint's regeneration; the group of spaces saving none went with the Group field (N1): GroupKept proves an edit leaves a saved group as it is |
| `remaining`: each ring stays until its field can save | `pane/model/profile_draft_unittest.cc` ProfileDraftTest.Remaining | twin | same steps and values ("http:", "example.com", "70000", "700") |

## ProfileFormTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `presets`: each platform's preset is the Electron app's | `pane/model/rules_unittest.cc` ProfileFormTest.Presets | twin | same three expectations; the loop over kWindows, kMacOS, kLinux is Swift's Platform.allCases |
| `hostGPUClaim`: the host's own platform claims the host's GPU; another keeps the preset's | `pane/model/rules_unittest.cc` ProfileFormTest.HostGPUClaim | twin | every other platform than the host's, the Swift's Windows on a Mac among them (made whole at the cutover) |
| `edits`: an edit keeps the stored fingerprint unless spoofing or the platform changed | `pane/model/rules_unittest.cc` ProfileFormTest.Edits | twin | same stored (Windows preset, timezone Europe/Lisbon), fresh (Linux preset), same five cases and results |
| `proxyString`: the proxy shortcut fills host, port, and the credentials it has | `pane/model/rules_unittest.cc` ProfileFormTest.ProxyString | twin | same seven inputs; C++ is stricter: expects {host 10.0.0.2, port 3128} where Swift compared the parse with itself, and Parsed() requires a parse where Swift's ?.password == nil passed on nil |
| `proxyStringIPv6`: an IPv6 host is no shortcut, and takes its brackets in one | `pane/model/rules_unittest.cc` ProfileFormTest.ProxyStringIPv6 | twin | same inputs and expectations (three nils, the bracketed host/port/user/password, cafe) |
| `proxyPasswordWithColons`: a pasted password keeps its colons | `pane/model/rules_unittest.cc` ProfileFormTest.ProxyPasswordWithColons | twin | same three; C++ also requires "proxy.example:1080:user:" to parse (Swift's ?.password == nil passed on nil) |
| `proxyPorts`: a port outside 1–65535 is no port | `pane/model/rules_unittest.cc` ProfileFormTest.ProxyPorts | twin | ProxyConfig.port is ProxyPort(); same six values and the two ProxyString inputs |

## ProxyPasswordVaultTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `roundTrip`: seals the passwords, which open with the same secret alone | `pane/model/mac/proxy_password_vault_unittest.cc` ProxyPasswordSealTest.RoundTrip | twin | same passwords and secrets; Swift expects a throw for another secret, C++ expects std::nullopt (OpenProxyPasswords returns optional) |
| `tampered`: a box changed since it was sealed does not open | `pane/model/mac/proxy_password_vault_unittest.cc` ProxyPasswordSealTest.Tampered | twin | same last-byte flip (^= 0x01) of the base64-decoded box; throw vs std::nullopt |
| `sealed`: an earlier Pane's box opens with the engine's key | `pane/model/mac/proxy_password_vault_unittest.cc` ProxyPasswordVaultTest.Sealed | twin | same in-memory Places (secret key "engine-key"); same read |
| `item`: the item an earlier Pane kept them in reads too | `pane/model/mac/proxy_password_vault_unittest.cc` ProxyPasswordVaultTest.Item | twin | same; the real Keychain item is also covered by ProxyPasswordStoreTest.ReadsTheirOwnItem (proxy_password_store_unittest.cc) |
| `unreadable`: a key the user denies, or another key, reads nothing | `pane/model/mac/proxy_password_vault_unittest.cc` ProxyPasswordVaultTest.Unreadable | twin | same: unavailable secret -> none; key "another" -> none; the box stays |
| `deletes`: once the profiles are sealed with them, neither the box nor the item stays | `pane/model/mac/proxy_password_vault_unittest.cc` ProxyPasswordVaultTest.Deletes | twin | same |

## ProxyPasswordsTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `filled`: the passwords an earlier Pane kept apart go back into its profiles | `pane/model/rules_unittest.cc` ProxyPasswordsTest.Filled | twin | same profiles (http proxy.example:3128, same dates) and stored map; passwords s3cret, nullopt, nullopt |
| `legacy`: a password the file still holds is read, and wins | `pane/model/rules_unittest.cc` ProxyPasswordsTest.Legacy | twin | same: NeedsProxyPasswordStore true; the file's "from-file" wins over stored "stale" |
| `needsStore`: only a proxy that signs in needs the store | `pane/model/rules_unittest.cc` ProxyPasswordsTest.NeedsStore | twin | same two inputs and results |

## ProxyRouteTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `ipv6Host`: an IPv6 host takes brackets for Chrome, typed with them or not, and none for a connection | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.Ipv6Host | twin | same values; Swift members (rules, isChallenger, connectionHost) are free functions ProxyRules, IsChallenger, ConnectionHost |
| `httpProxies`: HTTP and HTTPS proxies go to Chrome as they are, credentials and all | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.HttpProxies | twin | same |
| `socksWithoutCredentials`: SOCKS without credentials goes straight to Chrome | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.SocksWithoutCredentials | twin | same, socks4 with username "" included |
| `socksWithCredentials`: SOCKS with credentials goes through the local relay | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.SocksWithCredentials | twin | same |
| `challenger`: a challenge is the proxy's when it names its host and port | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.Challenger | twin | same four challenges against Proxy.example:3128 |
| `challengerForms`: a challenge is matched in the form Chrome names it, whatever was typed | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.ChallengerForms | twin | same IPv6, bracketed, default-port and punycode cases ("exämple.test" U+00E4 -> xn--exmple-cua.test) |
| `webRTC`: a proxied profile keeps all UDP to the proxy; others hide private interfaces | `pane/model/proxy_route_unittest.cc` ProxyRouteTest.WebRTC | twin | WebRTCPolicy.forProfile(proxied:) is WebRTCPolicyForProfile(bool); same two strings |

## ReorderTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `nearestPlace`: drops at the place whose top is nearest the copy's | `pane/model/rules_unittest.cc` ReorderTest.NearestPlace | twin | same ids a-d, heights 36, firstTop 0, spacing 4; same six tops and targets |
| `ends`: drops first or last past the list's ends | `pane/model/rules_unittest.cc` ReorderTest.Ends | twin | same: d@-400 -> 0, a@4000 -> 3 |
| `expanded`: counts an expanded profile's whole height | `pane/model/rules_unittest.cc` ReorderTest.Expanded | twin | same tall heights (b 144) and tops 140, 160, 170 -> 1, 1, 2 |
| `order`: moves a row to its target among the others | `pane/model/rules_unittest.cc` ReorderTest.Order | twin | Reorder.order is ReorderedIds(); same four moves and results |

## SecurityTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `electronHashes`: verifies the Electron app's bcryptjs hashes | `pane/model/mac/pin_hash_unittest.cc` PinHashTest.ElectronHashes | twin | Same three bcryptjs hashes verified (1234, 908172, 0000) and 1235 and "" rejected; VerifyPinHash stands for PinHash.verify |
| `twoA`: verifies an older $2a$ hash | `pane/model/mac/pin_hash_unittest.cc` PinHashTest.TwoA | twin | Same $2a$10$ hash and PIN 1234 |
| `malformed`: rejects a malformed hash instead of crashing | `pane/model/mac/pin_hash_unittest.cc` PinHashTest.Malformed | twin | Same three malformed hashes: "", "not a hash", "$2b$10$short" |
| `makes`: makes salted $2b$ hashes at the Electron app's cost | `pane/model/mac/pin_hash_unittest.cc` PinHashTest.Makes | twin | PinHash.make has no C++ counterpart (Pane makes no hash any more, pin_hash.h); the test helper MakeHash calls bcrypt_newhash(pin, 10, 61-byte buffer) exactly as PinHash.make did; same five expectations on 4321/4322 |
| `electronFile`: reads and writes security.json as the Electron app did | `pane/model/security_unittest.cc` SecurityStateTest.ElectronFile | twin | Same security.json input; pin length 4, failedAttempts 2, remaining 3; exact written JSON {"state":{"failedAttempts":0,"pin":null},"version":0}. C++ reads through PersistedFile::Load + SecurityState::FromValue |
| `recover`: a damaged file gives back its PIN when it still holds one | `pane/model/security_unittest.cc` SecurityStateTest.Recover | twin | Same truncated text (pin length 6, failedAttempts 3) and the same three nil cases (hash cut short, two NULs + garbage, $2b$10$short) |
| `recoverEscapedSlashes`: a damaged file from 0.13.1 gives back its PIN, its slashes escaped | `pane/model/security_unittest.cc` SecurityStateTest.RecoverEscapedSlashes | twin | Same input text and expected state; C++ escapes the slashes by hand, so the Swift check that JSONEncoder writes \/ (a Foundation precondition, not Pane code) has no counterpart |
| `unlock`: unlock submits on the stored length, and unlocks on success | `pane/model/security_unittest.cc` PinEntryTest.Unlock | twin | Same digits and actions: no title, not dismissable, 4 dots, verify 1234 on the fourth, none on a fifth, unlock on success |
| `wrongPin`: a wrong PIN shakes and starts over; the last one wipes | `pane/model/security_unittest.cc` PinEntryTest.WrongPin | twin | Same: 0000 with 2 remaining shakes, Shook empties and clears busy, 0000 with 0 remaining wipes |
| `setup`: setup asks for at least four digits, then the same again | `pane/model/security_unittest.cc` PinEntryTest.Setup | twin | Same titles (Enter new PIN, Confirm PIN), dots 4/5, CanContinue, steps, shake with "PINs don't match", setPin 12345 |
| `change`: change verifies the current PIN, then takes a new one | `pane/model/security_unittest.cc` PinEntryTest.Change | twin | Same: Enter current PIN, verify 1234, none on success with step enter and empty entry, setPin 9876 after confirm |
| `remove`: remove verifies the current PIN; a wrong one only shakes | `pane/model/security_unittest.cc` PinEntryTest.Remove | twin | Same: 1111 with 0 remaining only shakes, 1234 verified gives removePin 1234 |
| `unreadable`: an unreadable lock says why and takes no keys | `pane/model/security_unittest.cc` PinEntryTest.Unreadable | twin | Same: Unreadable("Can't read the PIN") is unlock, not dismissable, keeps the error, takes no digit |
| `editing`: backspace and non-digits | `pane/model/security_unittest.cc` PinEntryTest.Editing | twin | Same: "a" is none, 1 2 backspace leaves "1", two more backspaces leave it empty |
| `erases`: erases the data and caches, and nothing beside them | `pane/model/mac/panic_wipe_unittest.cc` PanicWipeTest.Erases | twin | Same folders, files, SingletonSocket link and hard-link witness; same stage/staged passes [1, 3]/erase checks, witness overwritten, outside/kept.txt and automation/kept.json kept |
| `erasesElectronData`: erases the Electron app's data the migration names, and its caches | `pane/model/mac/panic_wipe_unittest.cc` PanicWipeTest.ErasesElectronData | twin | Same layout and record (source, 2026-10-28, [a]); found directory standardized (StandardizedPath) and cache dir caches/@pane/veil; staged passes [1, 3]; @pane/Wiping gone; neighbor and migration.json kept |
| `refusesOtherFolders`: a record naming anything but the Electron app's data erases nothing of it | `pane/model/mac/panic_wipe_unittest.cc` PanicWipeTest.RefusesOtherFolders | twin | Same Documents source (staged nothing, kept.txt kept) and the same four refused sources: data, support, /Users, relative/path |
| `cutShort`: a wipe cut short is still staged, and what it cannot erase is reported | `pane/model/mac/panic_wipe_unittest.cc` PanicWipeTest.CutShort | twin | Same: staged copy holds Engine/Profile A/Cookies; a 0555 folder reports a failure ending Profile A/Cookies; the next erase is clean and Wiping goes |
| `cacheDirectory`: caches live apart only under Application Support | `pane/model/mac/panic_wipe_unittest.cc` PanicWipeTest.CacheDirectory | twin | Same paths and three results; BundleIdentifier.pane and kBundleIdentifier are both xyz.roundedcorners.pane |

## SleepSchedulerTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `unloadsHidden`: unloads a hidden profile once the timer elapses | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.UnloadsHidden | twin | same harness (clock 1_800_000_000; 15/30/300), tab sleep off, 31 min, unloads == 1 |
| `neverActive`: never unloads the active profile | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.NeverActive | twin | same: started with profile "active", tab "tab-1"; 31 min; unloads == 0 |
| `skipsKeepLoaded`: skips Keep loaded profiles | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.SkipsKeepLoaded | twin | same: "kept" Keep loaded; 31 min; unloads == 0 and nothing slept |
| `skipsProtected`: skips profiles holding a protected tab | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.SkipsProtected | twin | same: every tab protected; 31 min; unloads == 0 |
| `skipsUnloaded`: skips already-unloaded profiles | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.SkipsUnloaded | twin | same: no loaded tabs; 31 min; unloads == 0 |
| `offSetting`: does nothing when the timer setting is off | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.OffSetting | twin | same: profile unload off; 31 min; unloads == 0 |
| `restartsHiddenClock`: restarts the hidden clock when a profile is re-activated | `pane/model/sleep_scheduler_unittest.cc` ProfileUnloadTest.RestartsHiddenClock | twin | same: tab sleep off; 20 min, visit, away, 20 min -> 0 unloads; 11 more -> 1 |
| `sleepsBackground`: sleeps a background tab once the timer elapses | `pane/model/sleep_scheduler_unittest.cc` TabSleepTest.SleepsBackground | twin | same: tab-1 then tab-2 active; 16 min; slept == ["tab-1"] |
| `sleepsNeverActive`: sleeps a tab loaded in the background that was never active | `pane/model/sleep_scheduler_unittest.cc` TabSleepTest.SleepsNeverActive | twin | same: loaded override ["tab-1","tab-2"]; 15 min -> none slept; 1 more -> ["tab-2"] |
| `neverActive`: never sleeps the active tab | `pane/model/sleep_scheduler_unittest.cc` TabSleepTest.NeverActive | twin | same: tab-1 active; 16 min; nothing slept |
| `skipsProtected`: skips protected background tabs | `pane/model/sleep_scheduler_unittest.cc` TabSleepTest.SkipsProtected | twin | same: tab-1 protected, tab-2 active; 16 min; nothing slept |
| `offSetting`: does nothing when the tab timer setting is off | `pane/model/sleep_scheduler_unittest.cc` TabSleepTest.OffSetting | twin | same: tab sleep off; 31 min; nothing slept |
| `restartsBackgroundClock`: restarts the background clock when a tab is re-activated | `pane/model/sleep_scheduler_unittest.cc` TabSleepTest.RestartsBackgroundClock | twin | same: 10 min, re-activate tab-1 then tab-2, 10 min -> none slept; 6 more -> ["tab-1"] |
| `overBudget`: trims a profile that unloads over budget | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.OverBudget | twin | same: 400 MB cache, 31 min, trims == 1; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `underBudget`: leaves a profile that unloads under budget | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.UnderBudget | twin | same: 50 MB cache, 31 min, sizeReads == 1, trims == 0; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `budgetOff`: never measures the cache when the budget setting is off | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.BudgetOff | twin | same: budget off, 4000 MB, 31 min, sizeReads == 0, trims == 0; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `keepLoaded`: never trims a Keep loaded profile, however large | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.KeepLoaded | twin | same: Keep loaded, 4000 MB, 31 min, trims == 0; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `tabSleepPath`: never trims on the tab-sleep path | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.TabSleepPath | twin | same: unload off, 4000 MB, tab-1 then tab-2, 31 min, something slept, trims == 0; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `drainedByTabSleep`: trims a hidden profile that tab sleep drained | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.DrainedByTabSleep | twin | same: shown then hidden, 16 min, sorted slept ["tab-1","tab-2"], unloads 0, trims 1; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `drainedWithUnloadOff`: leaves a drained profile's cache when unloading is off | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.DrainedWithUnloadOff | twin | same: unload off, 400 MB, 16 min, slept ["tab-1"], sizeReads 0; C++ fake cache answers synchronously, so no settling or awaiting the trim task |
| `noStacking`: does not stack a second trim while one is in flight | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.NoStacking | twin | same: tab sleep off, trim never finishes, 31 + 31 min, unloads 2, trims 1; C++ holds the unanswered callback (Swift a Task sleeping 3600 s, cancelled at the end) |
| `afterFailure`: keeps ticking after a trim fails | `pane/model/sleep_scheduler_unittest.cc` CacheTrimTest.AfterFailure | twin | same: failing trim, 31 min, a warning, then 31 more, trims == 2; C++ fails synchronously (base::unexpected), so no await of trims["hidden"] |

## SwiftVectorsTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `vectors`: the files and keys the Swift writes | `pane/model/data_key_unittest.cc` SwiftVectorsTest.Vectors; `pane/model/data_key_unittest.cc` SwiftVectorsTest.SavedAsSwiftSavesThem | twin | the whole profiles literal (SwiftProfiles()), every field, compared at the cutover |
| `cppWrites`: the files the C++ port writes | `pane/model/data_key_unittest.cc` SwiftVectorsTest.CppWrites | twin, but | C++ reads test_data/cpp with the Swift test's expectations (every profile field, the settings, the PIN's length and attempts, the key opening); the published Swift Pane reading what the Views build wrote is `files-migration`'s swap pass, against 0.19.1 (ADR 0122) |

## TabSwitcherTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `ordersByRecency`: orders profiles and their tabs by most-recently-used | `pane/model/tab_switcher_unittest.cc` TabSwitcherGroupsTest.OrdersByRecency | twin | same profiles, mru ["p2","w1","p1"], max 8, and three expectations |
| `capsTotal`: caps the total number of tabs across groups | `pane/model/tab_switcher_unittest.cc` TabSwitcherGroupsTest.CapsTotal | twin | same inputs and expectations; the Swift sums tabs over every group, the C++ over groups 0 and 1 while GroupIds pins exactly two groups |
| `skipsUnowned`: skips tab ids that no profile owns | `pane/model/tab_switcher_unittest.cc` TabSwitcherGroupsTest.SkipsUnowned | twin | same: mru ["ghost","w1"] gives only work with w1 |
| `loadingTitle`: falls back to a loading title and leaves an absent favicon nil | `pane/model/tab_switcher_unittest.cc` TabSwitcherGroupsTest.LoadingTitle | twin | same: tab x1 with empty title and favicon gives "Loading..." and no favicon |
| `ordersWithinProfile`: orders tabs within a profile by most-recently-used | `pane/model/tab_switcher_unittest.cc` TabSwitcherGroupsTest.OrdersWithinProfile | twin | same: mru ["w2","w1"] gives work with [w2, w1] |
| `previousTab`: selects the previous tab's flattened position | `pane/model/tab_switcher_unittest.cc` InitialSelectedIndexTest.PreviousTab | twin | same: [a,b,c], previous "c" gives 2 |
| `previousGone`: falls back to the second item when the previous tab is gone | `pane/model/tab_switcher_unittest.cc` InitialSelectedIndexTest.PreviousGone | twin | same: previous "missing" gives 1 |
| `singleTab`: falls back to 0 when only one tab is present | `pane/model/tab_switcher_unittest.cc` InitialSelectedIndexTest.SingleTab | twin | same: one tab "only", no previous, gives 0 |
| `emptyList`: returns 0 for an empty list | `pane/model/tab_switcher_unittest.cc` InitialSelectedIndexTest.EmptyList | twin | same: empty list gives 0 |
| `runningOffsets`: returns the running tab-count offset for each group | `pane/model/tab_switcher_unittest.cc` GroupOffsetsTest.RunningOffsets | twin | same: mru ["w1","w2","p1"]; tab counts [2, 1] as two size checks, offsets [0, 2] (which also fixes two groups) |
| `noGroups`: returns an empty array for no groups | `pane/model/tab_switcher_unittest.cc` GroupOffsetsTest.NoGroups | twin | same: no groups gives no offsets |
| `advances`: advances within range | `pane/model/tab_switcher_unittest.cc` CycleTest.Advances | twin | same: cycle(0, 1, 3) == 1 |
| `wrapsForward`: wraps forward past the last index | `pane/model/tab_switcher_unittest.cc` CycleTest.WrapsForward | twin | same: cycle(2, 1, 3) == 0 |
| `wrapsBackward`: wraps backward past the first index | `pane/model/tab_switcher_unittest.cc` CycleTest.WrapsBackward | twin | same: cycle(0, -1, 3) == 2 |
| `emptyList`: returns 0 for an empty list | `pane/model/tab_switcher_unittest.cc` CycleTest.EmptyList | twin | same: cycle(0, 1, 0) == 0 |

## TimeZonesTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `regions`: the regions, in order, leave out what nobody would claim | `pane/model/rules_unittest.cc` TimeZonesTest.Regions | twin | same five identifiers; now is base::Time::Now(), Swift's default |
| `choices`: each choice names its place and its offset, sorted within its region | `pane/model/rules_unittest.cc` TimeZonesTest.Choices | twin | same identifiers and date (1_800_000_000 s); titles with U+2212 as Swift's \u{2212}; C++ also asserts the regions are exactly America, Asia |
| `city`: a zone's city, for the trigger and the note | `pane/model/rules_unittest.cc` TimeZonesTest.City | twin | same three; kUTCTimeZone is "UTC" as TimeZones.utc |
| `known`: every region lists what this Mac knows | `pane/model/rules_unittest.cc` TimeZonesTest.Known | twin | C++ lists ICU's zones by IANA name where Swift listed Foundation's; same eight region names, America/New_York present |

## ToolbarProgressTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `startsAtOnce`: a load shows at once, at least 5%, and fades in over 140 ms | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.StartsAtOnce | twin | same bar (width 300), load 0.02 at 10 s, progress 0.05, alpha 0 / >0.5 / 1 at 10, 10.07, 10.14 s; times are TimeTicks to the microsecond |
| `jumps`: each new value of a load jumps the bar to it | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.Jumps | twin | same: 0.1 at 0 s then 0.6 at 0.5 s gives progress 0.6 |
| `finishes`: a finished load fills the bar, holds 100 ms, and fades out over 140 ms | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.Finishes | twin | same steps and expectations: not started, progress 1, deadline 2.1 s, alpha 1 at 2.05, 0<alpha<1 at 2.17, 0 at 2.24 |
| `hidesForAnIdleTab`: a tab come forward with no load hides the bar at once | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.HidesForAnIdleTab | twin | same: tab changed to no load at 1 s: not started, alpha 0, no deadline |
| `showsALoadingTab`: a tab come forward mid-load shows its own progress | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.ShowsALoadingTab | twin | same: tab changed to 0.2 at 1 s: started, progress 0.2, alpha 1 |
| `hidesOnACrash`: a crash hides the bar at once | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.HidesOnACrash | twin | same: crash at 1 s gives alpha 0 |
| `slowMode`: five seconds with no news start the slow mode: a shimmer, and a fill that moves smoothly | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.SlowMode | twin | same frames at 4.9 and 5 s, news 0.8 at 5.1 s, and the 1/60 s loop to 20 s (monotonic, <= 0.8, ends at 0.8); shimmer != nil is EXPECT_TRUE(ShimmerAt) |
| `slowFinish`: in the slow mode, a finish races to full before it fades | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.SlowFinish | twin | same: finish at 6 s in slow mode, progress < 1 and alpha 1, then 1/60 s frames to full before 6.6 s, not started |
| `shimmerSpan`: the shimmer stays within the filled part and starts empty | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.ShimmerSpan | twin | same width 200, empty first span (ASSERT on it, as the Swift optional compare required), 1/60 s steps to 4 s within [0, 200] and 200 x 0.3; Swift span(at:) ignored its time, C++ CurrentSpan() takes none |
| `curves`: Material's curves, as Android's interpolators draw them | `pane/model/toolbar_progress_unittest.cc` ToolbarProgressTest.Curves | twin | same four checks; same control points (0,0,0.2,1) and (0.4,0,1,1), now gfx::CubicBezier::Solve |

## UpdatesTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `newer`: a newer release with the arm64 dmg is the available update | `pane/model/updates_unittest.cc` UpdatesTest.Newer | twin | same release JSON (v0.3.0, x64/arm64 dmg, exe) against 0.2.4; version and file name; lastPathComponent read as GURL::ExtractFileName |
| `notNewer`: the same or an older release is none | `pane/model/updates_unittest.cc` UpdatesTest.NotNewer | twin | same: v0.2.4 and v0.2.3 against 0.2.4 are nullopt |
| `noDmg`: a release without the arm64 dmg is none | `pane/model/updates_unittest.cc` UpdatesTest.NoDmg | twin | same: v1.0.0 with only the x64 dmg is nullopt |
| `untrustedDownloads`: a dmg Pane may not download is no update | `pane/model/updates_unittest.cc` UpdatesTest.UntrustedDownloads | twin | same 5 download bases and the 2 IsTrustedDownload URLs; GURL resolves the ".." segments before the check (Swift URL kept them), the results are still none/false |
| `standIn`: a stand-in release server serves its downloads from its own host | `pane/model/updates_unittest.cc` UpdatesTest.StandIn | twin | same server http://127.0.0.1:4100/releases/latest and downloads on :4200; version "1.0.0" from it, nullopt from GitHub's URL |
| `digest`: the dmg's SHA-256 comes from its digest, when GitHub gives one | `pane/model/updates_unittest.cc` UpdatesTest.Digest | twin | same 6 cases (hex "ab"x32, upper-cased, no digest, sha512:, 63 digits, 62 digits + "zz"); C++ also expects an update in the three release cases |
| `bareTag`: a tag without its v counts too | `pane/model/updates_unittest.cc` UpdatesTest.BareTag | twin | same: tag "1.0.0" gives version "1.0.0" |
| `failures`: JSON that is not a release, or a version that does not parse, fails the check | `pane/model/updates_unittest.cc` UpdatesTest.Failures | twin | same two inputs; C++ checks the exact errors (kNotARelease, kInvalidVersion) where Swift expected any throw |
| `eta`: the time left is a coarse bucket | `pane/model/updates_unittest.cc` UpdatesTest.Eta | twin | same 5 buckets: 0, 44 -> "<1min"; 45 -> "~1min"; 150 -> "~3min"; 3600 -> "~60min" |
| `average`: uses the average rate since the first bytes | `pane/model/updates_unittest.cc` DownloadETATest.Average | twin | same: start 1_800_000_000; 1 MB then 11 MB of 100 MB at +10 s -> 89 |
| `settles`: none in the first second, however slow the start | `pane/model/updates_unittest.cc` DownloadETATest.Settles | twin | same: 16_384, 32_768 at +0.5 s -> none; 15_032_768 of 163_000_000 at +1 s -> 10 |
| `hysteresis`: a shorter estimate shows at once; a longer one only after five seconds | `pane/model/updates_unittest.cc` DownloadETATest.Hysteresis | twin | same six updates of 100_000_001: none, 10, 10 (+20), 10 (+24), 26 (+26), 3 (+27) |
| `unknown`: no estimate before the size or the rate is known | `pane/model/updates_unittest.cc` DownloadETATest.Unknown | twin | same four updates, all none |
| `newConnection`: a new connection's rate replaces the last one's, and the time left holds until it is known | `pane/model/updates_unittest.cc` DownloadETAAcrossConnectionsTest.NewConnection | twin | same: start 1000, 135 MB; 670, held after RestartRate, then 3 |
| `goodRate`: a connection at a good rate stays | `pane/model/updates_unittest.cc` DownloadPaceTest.GoodRate | twin | same: window 5 s; 6 MB at +5 s stays; window still 5 s |
| `slow`: a slow connection, or a stalled one, is left at the end of its window | `pane/model/updates_unittest.cc` DownloadPaceTest.Slow | twin | same: 4 MB and 0 bytes at +5 s both leave |
| `givesUp`: each new connection no faster doubles the next window, and after four the download stays | `pane/model/updates_unittest.cc` DownloadPaceTest.GivesUp | twin | same windows 5, 10, 20, 40 each left; then 80 s and it stays |
| `cannotBeLeft`: a connection that cannot be left spends no switch, and its next window is no longer | `pane/model/updates_unittest.cc` DownloadPaceTest.CannotBeLeft | twin | same: six 5 s windows with can_leave false stay at 5 s; the next leaves, window 10 s |
| `firstBytes`: a window begun again at a connection's first bytes runs its length from there | `pane/model/updates_unittest.cc` DownloadPaceTest.FirstBytes | twin | same: time left 0 at +5 s; Begin(0, +4.5 s) -> 4.5 s left at +5 s (TimeDelta for TimeInterval) |
| `picksUp`: a connection that picks up starts the count over | `pane/model/updates_unittest.cc` DownloadPaceTest.PicksUp | twin | same: leaves at +5 s, window 10 s; 100 MB at +15 s stays, window 5 s |
| `resumed`: a resumed connection is judged by what it brought, not what came before | `pane/model/updates_unittest.cc` DownloadPaceTest.Resumed | twin | same: Begin(100 MB, +1 s); 101 MB at +6 s leaves |
| `target`: installs over a bundle Pane runs from in place, never a translocated or mounted one | `pane/model/updates_unittest.cc` UpdateInstallTest.Target | twin | same five bundle paths and answers |
| `staging`: stages the new version hidden beside the app, and knows its leftovers | `pane/model/updates_unittest.cc` UpdateInstallTest.Staging | twin | same: ".Pane-0.9.0.app"; leftover yes, "Pane.app" and ".Pane-notes.txt" no |
| `mounts`: mounts a dmg at a folder named for its process, and knows one whose process is gone | `pane/model/updates_unittest.cc` UpdateInstallTest.Mounts | twin | same pid 42 running, pid 7, the pre-0.13.1 UUID name, and the three names that are not mounts |

## VersionsTests.swift

| Swift test | `pane_unittests` | | Note |
|---|---|---|---|
| `newerPatch`: is true when the candidate's patch is newer | `pane/model/rules_unittest.cc` VersionsTest.NewerPatch | twin | IsNewerVersion returns base::expected<bool, InvalidVersion>; EXPECT_EQ(..., true) also requires a value, as Swift's try |
| `newerMinorOrMajor`: is true when the candidate's minor or major is newer | `pane/model/rules_unittest.cc` VersionsTest.NewerMinorOrMajor | twin | same two comparisons |
| `equal`: is false when the versions are equal | `pane/model/rules_unittest.cc` VersionsTest.Equal | twin | same; EXPECT_EQ(..., false) requires a value |
| `older`: is false when the candidate is older | `pane/model/rules_unittest.cc` VersionsTest.Older | twin | same |
| `leadingV`: strips a leading v from GitHub-style release tags | `pane/model/rules_unittest.cc` VersionsTest.LeadingV | twin | same two comparisons |
| `missingSegments`: treats missing trailing segments as zero | `pane/model/rules_unittest.cc` VersionsTest.MissingSegments | twin | same two comparisons |
| `malformed`: throws on a malformed version segment | `pane/model/rules_unittest.cc` VersionsTest.Malformed | twin | Swift's thrown InvalidVersion is the expected's error (InvalidVersion is its only error type): EXPECT_FALSE(IsNewerVersion("0.x.1", "0.1.0").has_value()) |
