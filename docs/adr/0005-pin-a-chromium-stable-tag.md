# Pane pins a Chromium stable tag, and a watch tells it when Chrome's Mac stable moves

The engine is built from one Chromium stable release, named by its tag in `chromium/CHROMIUM_VERSION` (158.0.8127.18 now), and the pin moves only through the upgrade routine in `chromium/UPGRADING.md`, which is how Pane takes Chromium's security fixes. Since Chrome 153 that is a milestone every two weeks and security updates every week, on the Mac stable channel (`chromium/UPGRADING.md`). The rest of the engine is made against the pin: the checkout is a clone of the tag (ADR 0007), each patch is the diff of its files against it (ADR 0008), `scripts/publish-engine.sh` refuses to archive an engine that is not the pinned version, and the engine key hashes `CHROMIUM_VERSION` (ADR 0049).

**The watch.** A scheduled workflow, `.github/workflows/chromium-watch.yml`, compares Chrome's Mac stable version with `CHROMIUM_VERSION` every six hours and, when Pane is behind, opens an issue for that version (none while one of the same title is open). Before it nothing noticed when Chrome moved: on 2026-10-30 Pane was on 157.0.8103.15, Google's Beta, while Mac stable was 157.0.8103.26, with 73 security fixes over two updates, 5 of them critical; Dia and Brave ship such fixes within one or two days (the research into how Aside, Dia, and other Chromium browsers ship them). The issue says whether Pane's patches apply to the new tag: `scripts/preflight.sh` fetches the files they touch at that tag from Chromium's gitiles and applies them, in under a minute, as Brave and ungoogled-chromium check theirs before a sync. Pane moved to 157.0.8103.26 that day.

**The first pins.** The first, on 2026-10-26, was 156.0.8078.48, the newest Mac stable that day. The pin moved to 157.0.8103.15 the same day. Mac stable was still 156.0.8078.48, but 157.0.8103.15 had reached the stable channel in its staged rollout (Windows, 2026-10-22), so the upgrade went to it, a new milestone, rather than to a 156 patch release: the milestone is the harder case for the runbook. Any stable release's tag builds for the Mac. The scripted routine (`scripts/`, ADR 0008) took the patches out, moved the checkout, and merged them back three ways, stopping on conflicts. One conflict changed what a patch does: 157 resolves `startRendering()`'s promise per rendered chunk (`OfflineAudioContext::OnChunkRendered`), before the completion event, so the audio noise moved there, ahead of both.

Two behaviors changed with Chrome 157, and Pane keeps both. Page fullscreen needs a focused page (`SuppressFullscreenFromUnfocusedView`, crbug.com/40052279): a page in a window that is not key is refused ("not granted"), and a user's click gives it focus. The `fullscreen` scenario had requested fullscreen from a page that had none, while another app was in use. It now does what the click does: the new automation command `activate` brings Pane forward, and then it clicks the page. 156 passed the old scenario under the same conditions and 157 refused it, so the change is Chrome's, not Pane's. A new extension is pinned to the toolbar (`kExtensionsPinnedByDefault`, on by default in 157). The Electron app showed every extension's button, so the default comes closer to it; `extension-management` expects a new extension pinned, then unpins and pins it.

What was rejected:

- **Checking Chrome's releases by hand:** it is what let Pane fall behind.
- **Building the new engine in CI:** a Chromium build takes hours and about 100 GB, which a GitHub runner does not have (ADR 0049's prebuilt engines).

How it is checked:

- **A run of the watch** (`workflow_dispatch`) logs both versions and opens the issue when they differ.
- **`scripts/preflight.sh 157.0.8103.26`** reported every patch applying; at 156.0.8078.48 it names 0006 and 0007.
- **Each move** runs the scenarios on the new engine; a failing one, run on the previous engine under the same conditions, tells Chrome's changes from Pane's (`chromium/UPGRADING.md`, step 5).
