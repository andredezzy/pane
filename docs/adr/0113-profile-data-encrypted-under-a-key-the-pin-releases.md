# Every profile's data is encrypted on disk, under a key the PIN releases

Everything Pane keeps of a profile is encrypted on disk: its tabs, cookies, site data, and caches. Without the data key nothing of it reads from the disk, and with a PIN set the key opens only with the PIN. It was the user's call (2026-10-30), after a test listed every profile's name and open tabs from `profiles.json` while the lock was set and no PIN had been typed.

What the test found, before this decision:

- **`profiles.json` was plain JSON** (`app/PaneKit/Sources/PaneKit/PersistedFile.swift`): each profile's name and tabs, with their addresses, titles, and favicons, open to any process of the user's.
- **The engine's profile folders were plain too, but for the cookies.** `DIPS` names the sites the user interacted with, `Network Persistent State` the hosts reached, and `Local Storage`, `IndexedDB`, and `Session Storage` hold the sites' data under their origins. The caches, which hold the pages themselves, sit under `~/Library/Caches/com.pane.desktop/Engine` (ADR 0006).
- **The PIN guarded the window, not the files** (ADR 0042). `security.json`'s bcrypt hash also let anyone with a copy try every PIN offline, where the panic wipe never counts.

How it works:

- **One encrypted volume holds every profile's folder** (PaneKit `DataVolume`). It is a sparse bundle in the data root (`Data.sparsebundle`, a terabyte at most and sparse) whose APFS volume is encrypted with a passphrase the data key derives, as FileVault encrypts a volume: in the kernel, its files' contents and names alike. The image attaches with its volume locked; once the key opens, the volume unlocks (`diskutil apfs unlockVolume`) and mounts at `Engine/Volume` through Disk Arbitration, in Pane's process and out of the Finder (`nobrowse`), its root the user's alone (0700).
- **It is made once**, at the key's first open, after the key is saved: an image with no file system (`diskutil image create blank`, where macOS has it; hdiutil's `-layout NONE` before), an empty APFS container on it (`newfs_apfs -C`), and in that one volume, born encrypted with the passphrase, which `diskutil apfs addVolume` reads from stdin (`DataVolume.attachOrMake`). No plain volume is ever made. An image whose making a quit cut short, with no container or no volume yet, holds nothing, and the next open finishes it; only an image just made, or one diskutil finds no container on, is formatted.
- **`Engine/<profile id>` and the profile's caches' folder are links into it** (`~/Library/Caches/com.pane.desktop/Engine/<profile id>`). Chromium allows a profile wherever its path's parent is the user data dir, comparing the paths as given (`ProfileManager::IsAllowedProfilePath`), and derives the caches' folder from the path alone (`GetUserCacheDirectory`), so the links need no patch.
- **The volume is inside the engine's user data dir** because the network service's sandbox allows only that and the caches' folder, by their resolved paths (`GetNetworkContextsParentDirectory`, `content/browser/sandbox_parameters_mac.mm`). Beside it, at the data root, the cookies and the HTTP cache did not open: the `migration` scenario's page saw its local storage and none of its cookies.
- **It fails closed.** Until the volume is mounted the links lead nowhere and the empty mount point is read-only (0500): a write into a profile fails rather than creating a plain folder. A mounted volume is kept only if macOS reports it encrypted (`volumeIsEncrypted`): a plain one put in its place unlocks with nothing and mounts, so its image detaches before anything is written, and no profile opens.
- **`profiles.json` is sealed in place**, AES-GCM over its JSON, bound to its name (PaneKit `SealedFile`), with each proxy's password in it. It opens without the volume, so the window shows the profiles and their tabs while the volume opens. A plain one an earlier Pane wrote is read once and sealed, with the proxy passwords it kept apart (ADR 0095's `proxy-passwords.json`, or the Keychain item before it), whose store then goes.
- **One data key seals both**: 256 random bits, with a key per use through HKDF (PaneKit `DataKey`). It is kept only sealed, under a key HKDF-SHA256 derives from the engine's "Pane Safe Storage" secret, which the engine already reads at every launch, so an update still asks once (ADR 0095). With a PIN, the derivation takes the PIN too, stretched with PBKDF2-HMAC-SHA256 at OWASP's 600,000 rounds (64 ms on this Mac, where bcrypt at cost 10 takes 40), with a salt of its own at every seal.
- **The sealed key tells another secret from a wrong PIN**: it keeps a check of the secret (HMAC-SHA256, cut to 128 bits). Without it, an engine key replaced would make every PIN look wrong, and the fifth would wipe the data. Another secret shows the lock's line for an unreadable key, and counts no attempt.
- **The PIN is checked by opening the data key**, and the bcrypt hash goes. A PIN set before this is checked once more against its hash, which then goes. A wrong PIN still counts on disk at once, and the fifth still wipes. Change and Remove check the PIN with the secret this session already read, so they ask the Keychain nothing.
- **The sealed key is written twice**, atomically: in `security.json`, and in its copy beside the volume, `Data.security.json`. Losing both loses the data; a second copy sealed the same way weakens nothing. A new key is written before the volume it opens is made (`AppLock.opened`): made first, a quit between them left a volume no saved key opens, which the next launch shows as a key lost.
- **Setting, changing, or removing the PIN reseals the data key** and nothing else: no data moves, nothing restarts.
- **Pane makes the engine's Keychain item when it has none and no key is sealed yet**, after `LaunchMigration` has run and before the engine starts, as Chromium makes it (16 random bytes, in base64, `keychain_password_mac.mm`): a fresh install needs the data key before the engine ever reads its item. ADR 0095 rejected this because a migration would then not hand over the Electron app's key; by this point the migration has run. A migration whose key macOS could not ask for (a locked session) is no longer retried: its sites stay signed out. Once a key is sealed Pane never makes a new secret, which would not open it.

What the engine opens on its own:

- **Chromium starts on its Default profile.** At startup it opens the profile Local State names as used last (`GetStartupProfile`, `chrome_browser_main.cc`), and the ones open last as its fallback: Pane's, before the PIN, outside every gate of Pane's. With an earlier Pane's plain data, it opened the profile the move was copying. Before the engine starts, Pane names Default instead and no profiles open last (PaneKit `LocalState`); Pane restores its own session.
- **The segmentation platform is off** (`SegmentationPlatform`, `SegmentationPlatformUkmEngine`, in `engine/pane/browser/browser_application.mm`). Its database at the user data dir's root (`segmentation_platform/ukm_db`) kept the addresses of the pages visited, outside the volume: the `security` scenario found a page's address there. An earlier engine's copy is erased at launch, as the panic wipe erases.
- **Everything else at the engine's root holds none of the browsing**: `Local State` names no profile and no site outside Google's infrastructure, and the `Default` profile, where Pane never browses, has no history, downloads, or favicons (counted on this Mac, 2026-10-30). The `security` scenario's search for a page's address and a value it stored covers the whole data root.

Launch and unlock:

- **The engine still starts before the PIN.** Chromium creates the application, and nothing may touch it before (`app/Pane/main.swift`).
- **The image attaches first, before the engine starts** (main.swift), with its volume locked and its watchdog: while the engine starts and the PIN is typed. One that fails is attached again once the key opens, a crash's leftovers cleared. `diskutil image attach --noMount` where macOS has it (26 and later), since hdiutil's attach is deprecated there, and hdiutil before.
- **The data's lock decides which Pane holds the image** (PaneKit `DataLock`): a `flock` on `Pane.lock` in the data root, taken before the attach, which the system lets go of with its process, a crash included, and which the processes Pane starts do not keep. Another Pane started on the same data, as from a terminal, hands its URLs over through the engine's singleton and quits, and leaves the image alone. The attach had counted on a Pane running to keep its image from a second attach, as hdiutil does; diskutil hands back the attached image's devices instead, and the second Pane's watchdog detached the first's volume as it quit, so the first's pages stopped loading (`data-volume`). A Pane started while the last one quits waits for the lock, which that one lets go of once it has detached the image.
- **The model starts with its settings alone**; the profiles come once the key opens. Until then, and while an earlier Pane's data moves in, nothing runs (`BrowserController.interactionAllowed`), as behind the lock.
- **The key opens off the main thread**, and the volume's unlock and mount start there, at once (`DataVault.attacher`): without a PIN at launch, with one as the PIN is checked, while the lock slides away (0.35 s, ADR 0042). After an update, the Keychain's one prompt now comes before the profiles show.
- **A profile's context opens only once the volume is mounted** and the profile's folders and links are in place (`BrowserController.withContext`, `DataVault`): the single way a profile reaches the engine.
- **The volume never outlives Pane.** After the engine stops, the image detaches by its device, once an attach still under way has finished (the first launch's making of the image, say: a quit in it had left the image attached, after Pane and before its watchdog), which takes it whole (a detach by the mount point can unmount the volume and leave the image attached), waiting a moment for the engine's helpers (the network service keeps the cookies open), then forcing. A watchdog, `pane-volume-watch` (`app/VolumeWatch`, built and signed by `package.sh`), starts as the image attaches, wakes once when Pane's process ends (kqueue's `NOTE_EXIT`, nothing polls), and detaches the device only while it still holds this image, since a device's number goes to the next image attached. It detached 321 ms after its process ended. It runs in a session of its own (`setsid`), ignoring the hangup of the terminal Pane was started from: a kill of Pane's process group had ended both, and left the volume mounted and unlocked. The space the sparse bundle keeps after deletions is compacted at quit, past 1 GB.
- **Measured on this Mac** (M5 Pro, 2026-10-30), `benchmark-unlock`, five runs each, medians, against the build before this:
  - From the PIN to the first page painted: 255 ms before, 474 and 492 ms now (+225; two sets of five). After the PIN's stretching, the unlock and the mount take 209 to 221 ms. The 328 ms first reported here was a plain volume's: the benchmark's setup quit while hdiutil was still making the image, and the next launches opened the plain volume it left, whose unlock failed at once and whose mount went on (`DataVolume.open` refuses one now, and the setup waits for the volume).
  - From launch to the first page without a PIN: 340 ms before, 612 and 636 ms now (+280). The image's attach, the unlock, and the mount follow one another, and the page waits for them.
  - The build before these fixes, measured the same way on the same afternoon: 540 and 548 ms from the PIN, 646 and 665 from launch.
  - The first build encrypted the image itself: 873 and 1,033 ms. Isolated, attaching an image takes 250 to 280 ms, unlocking its volume 170 to 190 ms, and mounting it through Disk Arbitration 47 to 62 ms.
  - The first launch, which makes the volume: from the attach's start to the volume mounted, 1,470 to 1,537 ms over three runs, where hdiutil's image with a plain volume took about 3.5 s; from launch to the volume mounted, 1.86 to 1.92 s, against 3.7 to 3.85. Its steps: the image 70 ms, its attach 170, the container 100, finding it 90, the encrypted volume 880, the unlock 190, the mount 50.

Moving the data in:

- **An earlier Pane's plain profile folders move into the volume** before any profile opens, file by file, each staged for the panic wipe's erase only once its copy is whole, so a move a quit cuts short copies that folder again. Their caches are dropped, and rebuild. The move follows the data, not the launch: plain folders found at any open move in.
- **Two states on screen change**, drawn in Paper and approved by the user (2026-10-30) on the Lock screen page (ADR 0058):
  - **07.5, the move's progress**: "Encrypting your data", the update's bar (`UpdateProgressBar`) at the numpad's width, and "Once, after this update", in place of the dots and the numpad. With no PIN to ask first, it shows from the first frame.
  - **07.6, an unreadable key**: "Pane can't read the key to your data. / Quit, then restore security.json.", in two lines, no longer saying to remove the file. The dots and the numpad show at 40%, since the lock takes no keys then. When the Keychain gives no secret, its second line asks to open Pane again and allow its keychain item (07.7, drawn while building, approved by the user, 2026-10-30).

The panic wipe:

- **The data key goes first.** Nothing left on this Mac opens the volume or the sealed file after that, the SSD's old blocks included; a copy made earlier with `security.json` is as safe as the Keychain and the PIN made it. The image is detached, and the volume and the files deleted, the image without random passes, which protect nothing more inside it. The fresh Pane seals a key of its own.

What stays outside, and why:

- **`settings.json`**: the lock's theme and the Dock icon show before the PIN (ADR 0001, ADR 0089), and it names no profile, site, or tab (the user's call, 2026-10-30).
- **`security.json`, its copy, and `migration.json`**: the lock and the wipe read them before any key.
- **`deletions.json`**: it names only profile ids, which Engine's links name anyway.
- **`Engine/`'s own files**, as above.
- **Pane's preferences**: the window's frame. The open panel's last folder, which AppKit keeps there, is removed at quit.
- **Nothing of the update check**: it, and its download, moved from `URLSession`'s shared and default sessions (`app/Pane/Updates/Updater.swift`) to ephemeral ones, which keep no cache in `~/Library/Caches/com.pane.desktop`.

What it cannot cover:

- **Code running as the user while Pane is unlocked.** The volume is mounted, and any process of the user's reads it, as it reads any app's files under FileVault. Until a Developer ID (ADR 0048) the build runs without the hardened runtime, so such code can also start Pane with a library of its own and read the engine's secret without a prompt.
- **A short PIN, against someone with the files and the Keychain secret together.** Offline the panic wipe never counts, and a few digits fall in seconds to hours whatever the stretching. A key that never leaves the Mac, in its Secure Enclave, needs the data protection keychain, so it waits for a Developer ID too.
- **What macOS keeps on its own**: a site's notifications, if any, in its notification database; and plain copies in Time Machine backups or local snapshots made before this (none on this Mac: no destination, no snapshots).

What was rejected:

- **Sealing only `profiles.json`**: the engine's folders name the same sites.
- **Keeping the caches out of the volume**: they hold the pages themselves (the user's call).
- **A volume only while a PIN is set** (the user's call, 2026-10-30): a profile without a PIN would stay in the clear; setting the PIN would copy its data in and leave the plain blocks on the SSD, and removing it would write everything back in the clear; and either would restart the engine.
- **Encrypting each of Chromium's stores** (the disk cache, SQLite, LevelDB, blobs): a patch in every storage layer, to carry through every upgrade (ADR 0008).
- **The PIN before the engine starts, with all of `Engine/` in the volume**: nothing may touch the application before Chromium creates it.
- **The volume beside `Engine/`**: the network service's sandbox does not reach it.
- **Encrypting the image itself** (`hdiutil create -encryption AES-256`), the first build's: its attach, with the image's own key derivation and a cipher in `diskimages-helper`, came after the key and took about 600 ms in Pane, so the first page came 873 ms after the PIN.
- **Mounting through `diskutil`**: 200 ms to start, where Disk Arbitration in Pane's process takes 50. `unlockVolume -mountpoint` mounts in one step, but in the Finder: it takes no `nobrowse`.
- **hdiutil's image with a plain APFS volume, the encrypted one added beside it and the plain one deleted**, the first build's: 3.5 s, and a quit before the delete left a plain volume that the next open mounted, in the clear.
- **`newfs_apfs -E`**, a container and an encrypted volume in 386 ms: it takes the passphrase on its command line, which the user's other processes can read, or from a terminal alone.
- **Making the image before the engine starts**: the lock takes an image with no key for a key lost, and a fresh install has no key until the Keychain gives the secret.
- **Opening the key and the volume before the engine starts, without a PIN**: measured, it gained nothing, since the image's attach, the unlock, and the mount follow one another whatever starts them.
- **A volume per profile**: an attach each, and a mount per profile.
- **An APFS volume in the Mac's own container**: a change to the Mac's disk for an app's data, which would outlive the app.
- **A Keychain item of the data key's own**: a second prompt at every update, which ADR 0095 removed.
- **A slower stretching of the PIN**: a few digits do not hold offline at any cost the unlock can wait for. The Keychain secret is what keeps a copy shut.
- **A watchdog that polls** (`kill -0` in a loop) or waits with `caffeinate -w`: one wakes the Mac every second for Pane's life, the other holds off its idle sleep.
- **Proxy passwords sealed apart from `profiles.json`**, as ADR 0095 kept them: the sealed file holds them as safely, and one place is less to keep in step.

How it is checked:

- **Before building** (2026-10-30), a scenario on the 0.13.1 build, in a throwaway home on the mock keychain, found that a profile whose folder is a link into a mounted volume loads through it, keeps the link, and writes nothing in the clear once the volume is detached; and that with the volume detached, opening the profile fails without writing. It checked local storage only: the network service's sandbox showed up later, in the `migration` scenario.
- **PaneKit's tests:**
  - `DataKey` opens with its secret and PIN only, tells another secret (`OtherSecret`) from a wrong PIN, and reseals the same key with a new salt.
  - `SealedFile` writes nothing in the clear, and a changed, moved, or plain file does not open.
  - `DataVolume` links a profile's folders, moves an earlier Pane's in whole and stages them for the erase, copies again what a cut-short move left, and the wipe takes its image without passes and the key's copy before `security.json`.
  - `LocalState` starts the engine on Default, and `ProfileDeletions` purges a linked profile and a plain one.
  - `DataLock` is one process's while it runs and the next one's once it lets go, waited for, and not kept by a process Pane starts.
- **Scenarios**, on the mock keychain (ADR 0013) and a real volume:
  - `security`: behind the lock the image is attached, its volume locked and mounted nowhere, the profile's link leads nowhere, and a page's address and a value it stored are in no file under the data root; the Electron app's hash gives way to the key sealed with the PIN, in both copies, a change reseals it with a new salt, and a removal seals it without one; the panic wipe destroys the key and erases the volume; the volume is detached after a quit, and, by its watchdog, after a `SIGKILL`; the watchdog runs apart from Pane's process group.
  - `proxy`: `profiles.json` is sealed, no file holds a proxy's password in the clear, the store is gone, and the Electron app's plain password signs in, sealed, before and after a relaunch.
  - `migration`: the Electron app's profiles move into the volume behind the PIN, and the page sees their cookies and local storage.
  - `profile-lifecycle`: a delete the engine never confirmed is purged once the volume is mounted.
  - `data-volume`: a second Pane on the same data hands over and quits, and the first keeps its volume and loads pages, where it lost both before; an image cut short before its container, then before its volume, is finished; a plain volume in the encrypted one's place is refused, detached, and holds nothing of the profile's; a quit while the first launch makes the image leaves nothing attached.
  - Every scenario starts once the profiles are open and the volume mounted, or the PIN screen shows (`tests/lib/pane.ts`), and fails at launch, saying why, when the volume would not attach; `benchmark-unlock` times what the volume adds.
- **A real update from the published app**: one Keychain prompt, as before, and the caches' link under `~/Library/Caches`, which scenarios do not reach, holding the HTTP cache.
