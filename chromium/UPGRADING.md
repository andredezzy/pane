# Upgrading Chromium

Pane ships Chromium's security fixes by moving the engine to each new stable tag: since Chrome 153 (2026-09) a new milestone every two weeks, and security updates every week. Follow the Mac stable channel, not Beta or Early Stable. The Chromium watch (`.github/workflows/chromium-watch.yml`, ADR 0005) opens an issue when Chrome's Mac stable is ahead of `CHROMIUM_VERSION`. The routine below takes the patches out, moves the checkout, puts the patches back (merging what moved), rebuilds, and proves the result with every scenario.

## 1. Pick the tag

The newest stable release for Apple Silicon Macs:

```bash
curl -s "https://versionhistory.googleapis.com/v1/chrome/platforms/mac_arm64/channels/stable/versions?page_size=1" | grep '"version"'
```

A milestone often reaches the stable channel on other platforms first, during its staged rollout (`platforms/win/channels/stable`). Any stable release's tag builds for the Mac.

Then see what the move takes, in under a minute and with no sync: `scripts/preflight.sh <tag>` fetches each file `patches/MANIFEST` names at the tag from Chromium's gitiles and applies every patch to them. It names each patch that no longer applies, with git's reasons; step 3 merges those. The watch's issue carries its output.

## 2. Move the checkout

```bash
scripts/apply-patches.sh --reverse   # the pristine tag; sync.sh refuses to move otherwise
echo <tag> > chromium/CHROMIUM_VERSION
scripts/sync.sh                      # fetch the tag, check it out, gclient sync, hooks
```

`sync.sh` keeps the checkout shallow. A patch release syncs in minutes. A new milestone moves most dependencies and toolchains, so it downloads several GB.

## 3. Put the patches back

```bash
scripts/apply-patches.sh
```

A patch that no longer applies is merged three ways from the blobs it was made against (the old tag's, still in the checkout). A conflict stops the run and lists the files that have conflict markers. For each:

1. Resolve the markers. Keep the patch's intent, which the ADRs record for each hook and seam (ADR 0010 for the startup seam, 0021 for the UI hooks), against the new code. If the hooked function moved or was renamed, follow it in the new tag.
2. Regenerate that patch: `scripts/make-patch.sh <patch>`. This also records it as applied.
3. Run `apply-patches.sh` again: it continues after the patches already applied.

Then rebase every patch onto the new tag, so none applies with offsets:

```bash
grep -v '^#' chromium/patches/series | xargs -n1 scripts/make-patch.sh
```

## 4. Build

```bash
mkdir -p evidence/logs
nohup scripts/build.sh > evidence/logs/build-<tag>.log 2>&1 &
```

A new milestone's build is the one to change what most of Chromium compiles in, such as `pane/BRANDING` (its `branding_file_path` in `chromium/args/release.gn`): `chrome_version.h` carries its names into most of the build (ADR 0114). A new `BRANDING` key at a milestone comes from `chrome/app/theme/chromium/BRANDING` at the new tag: take any it adds, and keep Pane's names. A patch release leaves it alone, for its own, cheaper build.

A new milestone rebuilds almost everything: 157 took 1h44m here (about 45,600 objects at 12 of 18 cores, on an idle Mac), and the sync before it about 25 minutes. `build.sh` runs 6 jobs by default, which leaves the Mac usable; 12 reset it by watchdog while it was in use (ADR 0007), so pass `PANE_BUILD_JOBS=12` only when nobody is using it. Run one Chromium build at a time, and keep the checkout out of Spotlight (`docs/building.md`).

What usually breaks, and where to fix it:

- **`//pane` no longer compiles**: Chrome's internal APIs drift between milestones (renamed methods, moved headers, changed callbacks). Fix `pane`, not Chromium.
- **A patched file failed to compile**: the merge in step 3 kept something the new code no longer has. Fix the file, then `make-patch.sh` its patch.

## 5. Prove it

```bash
scripts/package.sh
(cd app/PaneKit && swift test)
bun tests/run.ts --binary app/build/Pane.app/Contents/MacOS/Pane \
  --out evidence/upgrade-<tag> tests/scenarios/*.ts
```

Every scenario must pass. The ones that read the screen (CUA) need an unlocked session: on a locked one they skip or work around those checks, and `report.json`'s `complete` is false. Chrome UI that a new milestone adds, and that no hook covers, shows up as an extra window, which the scenario that saw it fails on (`WindowWatcher`).

A scenario that fails may have caught Chrome changing its own behavior (157 started requiring a focused page for fullscreen, and pinning new extensions). Run it on the previous engine under the same conditions to tell. Package the app on that engine's archive with `PANE_ENGINE_OUT` set to the unpacked archive, beside the new one. If the old engine passes and the new behavior is right for a browser, record it in an ADR and update the scenario to the new behavior. Otherwise fix Pane.

Then the data of earlier releases, which no other scenario reads across a move: `PANE_EARLIER_BUILDS=<published builds, comma-separated> PANE_REPAIR_FROM=<the published 0.18.0>` and `files-migration`, whose session has a cookie, every store, and a service worker. A new version can change what an old file means: 158 turned on `KeychainKeyProviderUserDataDirProtection`, which on a data directory other than Chrome's default makes the key every cookie is sealed under unavailable. Every test on its own build passed, and every user would have been signed out. So when this fails, read the milestone's diff of `chrome/browser/browser_process_impl.cc` and of what the failing store reads.

Then audit the stores (ADR 0121): a milestone adds some. `files-encrypted`, among the scenarios above, fails on a new store with no hook, and its result in `files-encrypted.json` lists every `unknown` file (the whole list is `files-inventory-list.json`): give the store a hook in `pane/encryption`, or an entry in `tests/lib/file-audit.ts`' allow-list with why it is neither. Run `pane_unittests`: its VFS leans on `sqlite3`'s shapes of I/O, and an upgrade of SQLite can add one.

## 6. Ship it

1. Commit `CHROMIUM_VERSION`, the patches, and the `//pane` fixes together: "Upgrade Chromium to <tag>", with `README.md`'s `Chromium-<milestone>` badge at a new milestone. Push it.
2. Publish the engine for CI and other machines: `scripts/publish-engine.sh`. It archives the engine unless `app/release` holds its archive, publishes it as the `engine-<key:12>` prerelease, checks GitHub's copy against the archive's SHA-256, and overwrites `prebuilt-engines.tsv`'s line with its own. Commit that line. It archives only an engine `build.sh` built from the committed sources: each build of `chrome` records their key in `out/Release/pane-engine.key`, the working tree's, so a build made before step 1's commit counts if nothing changed since. After any later change to them, build again.
3. Release the app as usual (`AGENTS.md`, Releasing).
