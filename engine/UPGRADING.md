# Upgrading Chromium

Pane ships Chromium's security fixes by moving the engine to each new stable tag: a patch release almost every week, a new milestone every four weeks. The routine below takes the patches out, moves the checkout, puts the patches back (merging what moved), rebuilds, and proves the result with every scenario.

## 1. Pick the tag

The newest stable release for Apple Silicon Macs:

```bash
curl -s "https://versionhistory.googleapis.com/v1/chrome/platforms/mac_arm64/channels/stable/versions?page_size=1" | grep '"version"'
```

A milestone often reaches the stable channel on other platforms first, during its staged rollout (`platforms/win/channels/stable`). Any stable release's tag builds for the Mac.

## 2. Move the checkout

```bash
engine/scripts/apply-patches.sh --reverse   # the pristine tag; sync.sh refuses to move otherwise
echo <tag> > engine/CHROMIUM_VERSION
engine/scripts/sync.sh                      # fetch the tag, check it out, gclient sync, hooks
```

`sync.sh` keeps the checkout shallow. A patch release syncs in minutes. A new milestone moves most dependencies and toolchains, so it downloads several GB.

## 3. Put the patches back

```bash
engine/scripts/apply-patches.sh
```

A patch that no longer applies is merged three ways from the blobs it was made against (the old tag's, still in the checkout). A conflict stops the run and lists the files that have conflict markers. For each:

1. Resolve the markers. Keep the patch's intent, which the ADRs record for each hook and seam (ADR 0010 for the startup seam, 0021 for the UI hooks), against the new code. If the hooked function moved or was renamed, follow it in the new tag.
2. Regenerate that patch: `engine/scripts/make-patch.sh <patch>`. This also records it as applied.
3. Run `apply-patches.sh` again: it continues after the patches already applied.

Then rebase every patch onto the new tag, so none applies with offsets:

```bash
grep -v '^#' engine/patches/series | xargs -n1 engine/scripts/make-patch.sh
```

## 4. Build

```bash
nohup engine/scripts/build.sh > evidence/logs/build-<tag>.log 2>&1 &
```

A new milestone rebuilds almost everything: 157 took 1h44m here (about 45,600 objects at 12 of 18 cores), and the sync before it about 25 minutes. Run one Chromium build at a time, and keep the checkout out of Spotlight (`docs/building.md`).

What usually breaks, and where to fix it:

- **`//pane` no longer compiles**: Chrome's internal APIs drift between milestones (renamed methods, moved headers, changed callbacks). Fix `engine/pane`, not Chromium.
- **`verify_order` fails** on `framework.order`: the exported Pane symbols changed order or set. Run `update-exports.sh`, rebuild, then `make-patch.sh 0001-pane-framework.patch`.
- **A patched file failed to compile**: the merge in step 3 kept something the new code no longer has. Fix the file, then `make-patch.sh` its patch.

## 5. Prove it

```bash
engine/scripts/package.sh
(cd app/PaneKit && swift test)
bun tests/run.ts --binary app/build/Pane.app/Contents/MacOS/Pane \
  --out evidence/upgrade-<tag> $(ls tests/scenarios | sed 's/\.ts$//')
```

Every scenario must pass. The ones that read the screen (CUA) need an unlocked session: on a locked one they skip or work around those checks, and `report.json`'s `complete` is false. Watch the window counts in the results: Chrome UI that a new milestone adds, and that no hook covers, shows up as an extra window.

A scenario that fails may have caught Chrome changing its own behavior (157 started requiring a focused page for fullscreen, and pinning new extensions). Run it on the previous engine under the same conditions to tell. Package the app on that engine's archive with `PANE_ENGINE_OUT` set to the unpacked archive, beside the new one. If the old engine passes and the new behavior is right for a browser, record it in an ADR and update the scenario to the new behavior. Otherwise fix Pane.

## 6. Ship it

1. Commit `CHROMIUM_VERSION`, the patches, and the `//pane` fixes together: "Upgrade Chromium to <tag>". Push it.
2. Publish the engine for CI and other machines: `engine/scripts/publish-engine.sh`. It archives the engine (`PANE_ENGINE_ARCHIVE=1 package.sh`) unless `app/release` holds its archive, publishes it as the `engine-<key:12>` prerelease, checks GitHub's copy against the archive's SHA-256, and adds its line to `prebuilt-engines.tsv`. Commit that line. It archives only an engine `build.sh` built from the committed sources: each build of `chrome` records their key in `out/Release/pane-engine.key`, the working tree's, so a build made before step 1's commit counts if nothing changed since. After any later change to them, build again.
3. Release the app as usual (`AGENTS.md`, Releasing).
