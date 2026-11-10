# The Views shell against 0.19.2

The Views build at the end of the shell (`docs/plans/views-migration.md`, H1 to H7, `main` at `3f62f15`, with the cuts below), against the published 0.19.2, by the budgets the plan sets for the shell's end: Phase 1's second table, and the gate's rows for scroll and a painted frame's way to the screen. Each from a copy (0.19.2 from its dmg, the Views build from `out/Release`), taking turns over three rounds of every benchmark (`tests/ab.ts`) on an M5 Pro on 2026-11-10; then three rounds of launch alone, since one launch of the third round failed (below). Another session's iOS Simulator ran the whole time, and the Mac's own media analysis took two cores for part of it: the one-minute load ran from 2.7 to 9.5, falling on both builds by the turns. Medians. Raw data: [views-shell.json](views-shell.json).

| Measure | 0.19.2 | Views | Change | Budget | |
|---|---:|---:|---:|---:|---|
| Launch to first window, cold (launch alone) | 267.9 ms | 254.1 ms | −13.8 ms | ≤ +5 ms | ✓ |
| Launch to first window, warm (launch alone) | 269.7 ms | 251.0 ms | −18.7 ms | ≤ +5 ms | ✓ |
| PIN to first page painted | 526.6 ms | 389.1 ms | −137.5 ms | ≤ +25 ms | ✓ |
| No PIN, launch to first page painted | 647.4 ms | 611.2 ms | −36.2 ms | ≤ +25 ms | ✓ |
| Memory, 1 tab | 148.0 MB | 145.4 MB | −1.7% | ≤ +1% | ✓ |
| … the browser process | 69.1 MB | 60.6 MB | −8.5 MB | ≤ 0.19.2's | ✓ |
| … the GPU process | 27.1 MB | 32.4 MB | +5.3 MB | | |
| Memory, 10 tabs | 351.8 MB | 343.5 MB | −2.4% | ≤ +1% | ✓ |
| … the browser process | 80.1 MB | 67.0 MB | −13.1 MB | ≤ 0.19.2's | ✓ |
| … the GPU process | 58.2 MB | 64.9 MB | +6.8 MB | | |
| Warm load, `smoke.html` | 5.2 ms | 5.2 ms | 0 | ≤ +3% | ✓ |
| Warm load, `find.html` | 7.7 ms | 4.9 ms | −36.4% | ≤ +3% | ✓ |
| Warm load, `nav-a.html` | 8.4 ms | 6.0 ms | −28.6% | ≤ +3% | ✓ |
| Warm load, Wikipedia | 107.0 ms | 118.4 ms | +10.7% | ≤ +3% | over, the site's first byte |
| Warm load, Apple | 410.6 ms | 404.9 ms | −1.4% | ≤ +3% | ✓ |
| Warm load, GitHub | 381.0 ms | 376.3 ms | −1.2% | ≤ +3% | ✓ |
| Scroll, frames dropped of 480 | 0 | 0 | 0 | ≤ 0.19.2's | ✓ |
| A painted frame reaching the screen, median | 19.55 ms | 19.51 ms | −0.04 ms | ≤ +1 ms | ✓ |
| … p90 | 20.10 ms | 26.96 ms | +6.86 ms | | |

What the table says:

- **Every budget holds but a real site's load.** Wikipedia's warm load reads 10.7% slower for its first byte: two of the Views build's nine loads waited 63 and 69 ms for it, where it usually comes at 27, and one of 0.19.2's took 220 ms. A real site's load is the site's, as the 0.19.0 candidate's run found ([README.md](README.md)).
- **Memory is lower than 0.19.2's,** by 2.6 MB at one tab and 8.3 at ten. The browser process, Pane's own, is 8.5 and 13.1 MB lighter, since AppKit and SwiftUI no longer draw in it; the GPU process, where Views composites, is 5.3 and 6.8 MB heavier. At the shell's end it was 32.6 MB heavier, and the total 15.1% over (below).
- **A launch is faster:** 14 ms cold and 19 ms warm to the first window, since the window rasters on the CPU and compiles no pipelines first (below). The three rounds of everything read −7.3 and −24.7 ms, over two rounds of the Views build: one of its launches in the third never brought up DevTools within a minute, before anything draws, in the data's launch; none of the 42 launches after it stalled.
- **Unlock is faster too,** 137 ms from the PIN, as at the shell's end (−104 ms then): the Views build attaches the volume sooner.
- **A painted frame reaches the screen as it did:** each of the page's frames in a scroll, from its compositor's submission to its presentation (cc's `SubmitCompositorFrameToPresentationCompositorFrame`), as the gate measured it on 2026-11-09 (18.86 against 18.73 ms then). The p90 is 7 ms longer on Views; on 2026-11-09 the two were level (26.57 against 26.81), so it is 0.19.2's slowest tenth that moved, not the Views build's.

## What the GPU process held for the window

The Views UI composites in the GPU process, where the Swift's AppKit drew in the browser's. At the shell's end (`3f62f15`) its GPU process held 32.6 MB more than 0.19.2's at one tab (58.5 against 25.9 MB) and 31.2 MB more at ten, while its browser process held 10.6 and 16.3 MB less. A detailed memory dump of each (`disabled-by-default-memory-infra`), cc's layer snapshot, and `footprint`'s categories showed where:

- **The window's tiles, 19.4 MB.** cc tiles a painted layer, and on the GPU it tiles a wide one in stripes as wide as the window, 2560 × 416 pixels. The window's own layer held the content panel's card, ring, and shadow, the toolbar, and the sidebar's header and footer: four stripes, 16.25 MB. cc skips a tile only when what's painted in it is one color, and gives up at the first path a view draws, over that view's whole bounds, so every stripe rastered. The sidebar's rows held a tile of 576 × 1,408, 3.09 MB, as tall as the list, since the scroll view keeps its contents at least that tall.
- **GPU rasterization's own, about 8 MB:** Skia Graphite's pipelines and the Dawn worker threads that compile them (10 worker threads where 0.19.2's GPU process has 4, 2.5 MB of stacks), its caches (2.5 to 4 MB of malloc), and its atlases (3.3 MB of graphics memory).
- **Not the window's output buffers.** Three full-window buffers, 47 MB, exist on both builds, 0.19.2's for its hosting window, and neither draws into them, since every quad of the window goes to Core Animation: they count in no footprint.

What was cut, with nothing on the screen changed:

| Cut | Tiles, one tab |
|---|---:|
| The window's layer holds its color alone (`RootView`), so it has no tiles | 16.25 MB → 0 |
| The card, ring, and shadow are four bands along the card's edge, each a layer painting the whole frame clipped to itself, so its pixels are a single painting's, and a middle the compositor fills (`PanelFrame`) | → 1.80 MB |
| The toolbar, the sidebar's header and footer, the empty state, and the sad tab on layers as small as they are | → 1.70 MB |
| The sidebar's rows on a layer only as tall as they are | 3.09 → 0.56 MB |
| Pane's views raster on the CPU (ADR 0123) | the GPU process 7.2 MB lighter, GPU rasterization's own |

## Rastering Pane's views on the CPU

The same code rastering on the GPU, with the tiles cut, held ten tabs and missed one, and a cold launch's first window came later than 0.19.2's. Three rounds against 0.19.2, as above:

| Measure | 0.19.2 | Views, GPU raster | Change | Budget | |
|---|---:|---:|---:|---:|---|
| Launch to first window, cold | 264.0 ms | 273.6 ms | +9.7 ms | ≤ +5 ms | **over** |
| Launch to first window, warm | 265.6 ms | 266.5 ms | +0.8 ms | ≤ +5 ms | ✓ |
| Memory, 1 tab | 146.6 MB | 151.0 MB | +3.0% | ≤ +1% | **over** |
| … the GPU process | 26.0 MB | 38.5 MB | +12.5 MB | | |
| Memory, 10 tabs | 349.8 MB | 350.1 MB | +0.1% | ≤ +1% | ✓ |
| … the GPU process | 57.2 MB | 70.4 MB | +13.2 MB | | |
| A painted frame reaching the screen, median | 18.94 ms | 18.83 ms | −0.11 ms | ≤ +1 ms | ✓ |

A cold launch traced from its start (`--trace-startup`, three each) shows why: on the GPU, Graphite compiles the window's pipelines on Dawn's worker threads before its first frame, which reached the screen 178 to 181 ms after the browser's start (172 to 177 on `3f62f15`), and 145 to 147 rastered on the CPU.

On the CPU each surface rasters on the browser compositor's one raster thread the first time it shows. From the input to the frame on the screen, in the background, medians of 30 rounds (10 for `views-settings`'):

| | GPU raster | CPU raster |
|---|---:|---:|
| A tab switch (⌘1, ⌘2) | 10.5 ms | 9.2 ms |
| New tab (⌘T) | 27.3 ms | 22.0 ms |
| Address field (⌘L) | 9.7 ms | 9.3 ms |
| Tab switcher (⌃⇥) | 8.5 ms | 9.2 ms |
| Quit Pane? (⌘Q) | 12.8 ms | 20.7 ms |
| Settings | 12.3 ms | 21.6 ms |
| A Theme change | 22.5 ms | 23.8 ms |
| App icon picker | 22.3 ms | 27.0 ms |
| A sky chosen | 13.5 ms | 13.8 ms |

Quit Pane?'s first frame took 37.8 ms on the CPU until its shadow and the switcher's were cast by a rect rounded at 0.57 of the radius (ADR 0057), which Skia blurs once where it works a path's blur out again for each tile; the two shadows stay within 2/255 of the squircle's in the renders. On the GPU that rounded rect costs a new pipeline the first time the dialog shows, 19 frames dropped, so it comes only with CPU raster; on the CPU the first showing drops none.

On the screen, CPU raster draws what a render paints: the renders (`pane/ui/render.cc`, which every surface was compared with the Swift's and Paper by) are CPU raster. On the GPU an icon's or the panel's corner's edge pixels differed from the render by up to 47/255 (616 of the toolbar's pixels by more than 2); on the CPU 100 do, by at most 5, at the profile chip. Between the two rasters, 1,218 of the window's pixels differ by more than 2/255 in light and 1,235 in dark, all at paths' edges, none in text.

## The first frames back

What CPU raster cost four surfaces' first frames, and what won it back (ADR 0123), on `30653a9` and after. Each first showing was traced from a test copy in the background: a Chrome trace of the browser and GPU processes cut at the app's own signpost for it, cc's snapshot of each frame, and sample(1) (`evidence/views-migration/first-frames-probe`). The timings and budgets below are `firstFramesBack` in [views-shell.json](views-shell.json).

A frame shows a refresh later once the work from its BeginFrame to Core Animation outgrows one refresh, 8.33 ms at 120 Hz. Rastered on the GPU that work took about 6 ms, on the CPU 10 to 11, which is the 8 to 9 ms the surfaces lost. Settings on `30653a9`, from its BeginFrame: the main frame 2.5 ms (painting its contents 2.0, a third of it shaping text again), the commit and its tiles' priorities 1.2, raster 4.6 (44 tiles of its contents on the one raster thread), the draw 0.5, and the display compositor 1.3.

From the input to the frame on the screen, medians, in the background, the final tree against `30653a9`, taking turns, on an M5 Pro in use on 2026-11-10 (another session's iOS Simulator; the one-minute load 2.0 to 8.8): `views-settings` (10 rounds a run), its rounds pooled over eight runs of each, and `views-key-timing` (30 rounds), the median of three runs' medians (`evidence/views-migration/first-frames-probe/final-ab`).

| | GPU raster | CPU raster, `30653a9` | Now |
|---|---:|---:|---:|
| Settings | 12.3 ms | 20.4 ms | 16.0 ms |
| Quit Pane? (⌘Q) | 12.8 ms | 15.6 ms | 11.8 ms |
| App icon picker | 22.3 ms | 26.8 ms | 17.7 ms |
| A Theme change | 22.5 ms | 22.5 ms | 22.7 ms |
| A sky chosen | 13.5 ms | 13.9 ms | 13.6 ms |
| A tab switch (⌘1, ⌘2) | 10.5 ms | 10.0 ms | 10.6 ms |
| New tab (⌘T) | 27.3 ms | 25.3 ms | 23.6 ms |
| Address field (⌘L) | 9.7 ms | 7.2 ms | 8.4 ms |
| Tab switcher (⌃⇥) | 8.5 ms | 8.4 ms | 8.5 ms |

A run's median moves more than some of these differences: over the eight pairs, `30653a9`'s Settings read 12.4 to 23.9 ms and the final tree's 13.6 to 21.6, a Theme change 17.0 to 24.8 and 19.0 to 24.5. Pair by pair, the App icon picker was faster in all eight (by 3.7 to 15.2 ms) and Settings in six (by 2.8 ms on average); a Theme change and a sky were level (+0.4 and −0.2 ms on average). Quit Pane? was faster in each of its three pairs; the four holds stayed within their runs' swing (⌘L 5.4 to 9.0 ms on `30653a9`, 7.5 to 8.5 now), and none of the changes runs in them but the labels', which only takes work away. An hour earlier, two runs each of the tree before its last edit read Settings 20.6 → 16.5 ms, Quit Pane? 19.3 and 19.7 → 13.8 and 12.9, the picker 27.7 → 15.4, and a Theme 22.7 → 24.9.

| Change | What it saved |
|---|---|
| Pane's labels keep their laid-out text while hidden (`pane/ui/design/text.cc`): Views drops it at every hide | Settings' paint 2.0 → 0.3 ms; the picker's 2.7–3.6 → 0.3, its ↩ shaped through 41 fallback fonts at each showing |
| A dialog grows and fades on cc's own animation (`CompositorMotion`, `pane/ui/design/motion.cc`): Pane's curves exactly, the first frame a frame in, the raster scale its end's | Quit Pane?'s entrance 64–96 raster tasks → 32; one tiling at scale 2, where two (17 MB) were held mid-entrance |
| Settings' cards and a dialog's card paint their squircle in pieces (`FillSquircle`, `StrokeSquircleBorder`) | Settings' raster 5.0 → 4.0 ms |
| The card paints in its shadow's layer | Quit Pane? 32 → 24 tiles; the picker 46–49 → 34–36 |
| The scrim and the card fade apart, as the Swift's `modal()` faded them | the display compositor's first frame 1.2–2.8 → 0.4–0.7 ms, no pass the window's size |

What stays: Settings' first frame rasters 44 tiles in about 4 ms on the one raster thread, which content makes (`VizProcessTransportFactory`) with no seam for more; a tile whose memory the GPU last held clears in up to 110 µs where a warm one takes 10. Its chain from the BeginFrame is now about 7 ms, at the refresh's edge: the median falls on one side or the other from run to run (13.6 to 21.6 ms over the final tree's eight runs).

The shell's budgets hold, against 0.19.2, taking turns (`tests/ab.ts`) on the same Mac in use: launch on the final tree, three rounds, of which one launch of each build never served DevTools, as one of 0.19.2's did at the shell's end, so two each; memory on the tree before its last two edits (the Dock's beat out, the at-once path), which hold no memory, three rounds. Both builds' launches ran slower than in the table at the top.

| Measure | 0.19.2 | Now | Change | Budget | |
|---|---:|---:|---:|---:|---|
| Launch to first window, cold | 270.1 ms | 253.3 ms | −16.8 ms | ≤ +5 ms | ✓ |
| Launch to first window, warm | 275.8 ms | 249.1 ms | −26.7 ms | ≤ +5 ms | ✓ |
| Memory, 1 tab | 150.6 MB | 146.7 MB | −2.6% | ≤ +1% | ✓ |
| … the browser process | 69.6 MB | 61.3 MB | −8.3 MB | ≤ 0.19.2's | ✓ |
| … the GPU process | 27.1 MB | 32.3 MB | +5.3 MB | | |
| Memory, 10 tabs | 351.8 MB | 345.7 MB | −1.8% | ≤ +1% | ✓ |
| … the browser process | 80.3 MB | 67.1 MB | −13.1 MB | ≤ 0.19.2's | ✓ |
| … the GPU process | 58.2 MB | 64.9 MB | +6.8 MB | | |

The renders are those of `30653a9` within 1/255 but for the update row's clock, which runs on, and two shapes now rastered 192 × 144 pixels further into their layer: the bottom row of Quit Pane?'s icon tile (7 to 11 pixels, 3 to 7/255) and the top of the picker's chosen tile in light (6, 3 to 5/255), where Skia's coverage along a curve's near-flat stretch moves with its place, as a tile's edge moves it on the screen. Against the Swift's renders the share of pixels over 2/255 is the same or smaller on every Settings and picker surface (`surfaces-updates-available` 2.13 → 1.89%), and Quit Pane?'s 2.63 → 2.64%. The final tree's captures, light and dark, are pixel for pixel those of the tree these were compared on, but for the clock, the overlay scroll bar's fade, and a download's progress.

## A page's first frame

On the three local fixtures, the first contentful paint read 8 to 12 ms later on the Views build, though the page painted 3 to 9 ms sooner; real sites didn't show it. Traced over 24 loads a build (8 rounds of the three fixtures, as `benchmark-pages` loads them, the `benchmark` category, and `gpu`'s `CommitPresentedFrameToCA`), medians:

| | 0.19.2 | Views (`3f62f15`) | Views (CPU raster) |
|---|---:|---:|---:|
| First contentful paint, painted | 23.1 ms | 14.9 ms | 15.4 ms |
| … presented (4-ms steps) | 28.0 ms | 36.0 ms | 36.0 ms |
| The page's first frame, submitted to presented | 2.4 ms | 16.8 ms | 15.8 ms |
| … received to the display's draw | 1.3 ms | 4.7 ms | 3.2 ms |
| … swapped to presented | (none) | 10.3 ms | 10.4 ms |

- **About 10 ms is how the frame is stamped, not when it shows.** On the Mac the GPU process stamps a frame's presentation with the time its display link predicts the frame reaches the screen, or with its commit while the surface has no display link timing yet. 0.19.2 drew each page through a display of the page's own, made new at each navigation: every commit during these loads was stamped at the commit (`now_to_display` 0, 3 of 3). The Views build draws the page through the window's display, which has the timing: every commit stamped at the predicted time (84 of 84), a median of 10.4 ms after the commit. That is the stage cc names `SwapEndToPresentationCompositorFrame`, which 0.19.2's first frames don't have at all. Both builds' pixels reach the screen through the same WindowServer, about a frame after the commit.
- **About 3 ms is real:** the window's display starts drawing the page's first frame 2 to 3 ms later (`ReceiveCompositorFrameToStartDraw`), since it waits for the window's own frame, which a navigation changes too. The page paints 8 ms sooner, so its first frame reaches the screen about when 0.19.2's did.
- **A real site shows neither,** its first paint coming 100 ms and more after the navigation, when 0.19.2's page display has its timing too.

## ⌘T's first frame

⌘T read 20 to 24 ms from its key to its frame on the screen (16 to 28 by run), where a tab switch read 10.6 and ⌘L 8.4. It was traced from a test copy in the background (the browser's and the GPU process's threads, cut at the app's signpost), sampled (`sample` over 40 ⌘Ts and 60 tab switches), and timed again by `views-key-timing`, which now reads three ends for each frame (`evidence/views-migration/cmdt-probe`):

- **Most of it was where the interval ended.** An interval ends when the UI thread hears its frame reached the screen, less how late it heard it (`late=`), and on the Mac the GPU process stamps a frame with its display link's prediction of the screen, about a refresh and a half after it commits the frame to Core Animation (A page's first frame, above). A tab switch's and ⌘L's ends are heard before that prediction, about a refresh after the commit, so they ended when heard; ⌘T's is heard after the page it makes once its frame is out, so it ended at the prediction, about 10 ms later for a frame that was no later. By its commit to Core Animation, the point 0.19.2's own intervals end at (`AfterFrame.committed`, just after Core Animation's commit), ⌘T's frame on `82e9748` came 7.2 ms after its key, a tab switch's 6.1 and ⌘L's 2.7 (the table after next).
- **Every page Chrome made asked LocalAuthentication** whether the Mac can authenticate its user: Chrome's autofill client, made with each page, asks it for Autofill AI's Google Wallet passes, which need the browser sign-in Pane turns off, through a synchronous call to the system's daemon. In the sample it was 162 of the page's 267 samples, on the UI thread, in every new, woken, and restored tab's page. It is off now (`pane/app/engine_switches.cc`, ADR 0022).
- **The key's own work cost the frame a refresh, often.** Asked for frames, the display compositor answers at once with a frame due at the next refresh, which cc drops when the UI thread reads it after that. ⌘T's 4.6 ms on the UI thread, about 1 of them AppKit laying the hidden title bar out for "Pane", outlasted it in 9 of 14 traced rounds, and a tab switch's in 4 of 6, and the frame then waited for the display link's next call, 6 to 12 ms away when it had stopped while the window was still. "Pane" follows the frame now, as a shown tab's title already did, and the frame that answers a command or a tab's choice is asked for once it is handled (`FrameHold`, `pane/ui/after_frame.h`): none dropped in 22 traced rounds.

From the key to the frame, in the background, the median of five runs' medians (and of their p90s), taking turns with `82e9748`, 30 rounds a run (`views-key-timing`, `evidence/views-migration/cmdt-probe/final-ab`). The first two columns are the rigs' reading, `late=`; the last two are the same frames' commit to Core Animation and the screen as the GPU process predicts it, which `82e9748` doesn't say:

| | `82e9748` | Now | Now, to its commit | Now, to the screen, predicted |
|---|---:|---:|---:|---:|
| New tab (⌘T) | 21.3 ms (23.7) | 17.0 ms (19.5) | 6.2 ms (7.5) | 19.6 ms (23.5) |
| A tab switch (⌘1, ⌘2) | 11.1 ms (15.2) | 11.3 ms (15.9) | 6.5 ms (8.1) | 20.6 ms (23.3) |
| Address field (⌘L) | 8.3 ms (12.4) | 8.5 ms (12.6) | 3.3 ms (4.0) | 15.2 ms (21.1) |
| Tab switcher (⌃⇥) | 9.6 ms (12.7) | 9.7 ms (13.4) | 4.6 ms (5.5) | 18.5 ms (22.3) |
| Quit Pane? (⌘Q) | 13.2 ms (18.0) | 12.3 ms (16.4) | 7.2 ms (8.7) | 21.0 ms (23.7) |

Against 0.19.2, in the background, taking turns, three runs each of 30 rounds (16 wakes), the median of the runs' medians (and of their p90s) (`evidence/views-migration/cmdt-probe/ab-key-timing.ts`, the same keys through either build's channel). 0.19.2's intervals end at its commit to Core Animation; `82e9748` is read through a build of it that also says its frames' commits (`meas`: the signposts' fields alone). A woken tab is timed to its page's first paint, which 0.19.2 doesn't time:

| | 0.19.2, to its commit | `82e9748`, `late=` | … to its commit | Now, `late=` | … to its commit |
|---|---:|---:|---:|---:|---:|
| New tab (⌘T) | 21.6 ms (31.4) | 21.4 ms (27.8) | 7.2 ms (11.0) | 16.5 ms (18.5) | 5.6 ms (7.2) |
| A tab switch (⌘1, ⌘2) | 16.0 ms (16.6) | 11.0 ms (15.7) | 6.1 ms (8.2) | 9.1 ms (12.8) | 6.0 ms (7.4) |
| Address field (⌘L) | 13.1 ms (20.7) | 7.4 ms (11.2) | 2.7 ms (4.4) | 7.6 ms (11.9) | 2.9 ms (7.2) |
| A tab woken | | 64.6 ms (70.7) | 56.8 ms (63.1) | 54.2 ms (57.8) | 45.5 ms (49.6) |

0.19.2's ⌘T read 31.1, 21.4, and 21.6 ms by run, as it read 12 to 21 by session before. ⌘L's p90 to its commit is a few slow rounds of thirty: 4.0, 7.2, and 7.2 by run here, 4.0 over the five runs above. The woken tab's 10 ms are the page made without LocalAuthentication's call; a new tab's are the same, after its frame.

What each end is for: 0.19.2's intervals end at its commit to Core Animation, so the budget's comparison is by the commit, the same point on both, after which both builds' frames take the same WindowServer to the screen. The predicted screen is read the same way for every interval of the Views build: when, by its display link, the frame reaches the glass, a refresh and a half after its commit. `late=` ends at the earlier of that prediction and when the UI thread hears of the frame, so it reads ⌘T, whose UI thread makes a page right after the frame, later than the others for a frame of the same age; `views-key-timing` now prints all three, and the other rigs still read `late=`.

What stays: hiding the page left (its NSView hidden, Chrome's WasHidden, the view removed) is about 1.2 ms of a tab switch's 2.7 ms on the UI thread and 0.8 to 1 of ⌘T's, which the Swift put after the frame and Views does in Chromium's swap; H5 read 0.18 ms a switch from a sample it took at 1 ms a sample, where `sample` ran at 2 to 3 here. A row added to the sidebar rasters the rows' layer whole (a layer whose size changes paints again), 8 to 12 tiles, about 1 ms on the one raster thread. And the display link, stopped after 20 still refreshes, takes 6 to 12 ms to call back, which `late=` waits on for ⌘T's presentation to be heard.

The budgets hold on this tree, against 0.19.2, three rounds taking turns (`tests/ab.ts`): launch to the first window −20.2 ms cold and −16.5 warm; memory −2.0% at one tab and −2.5% at ten, the browser process 8.3 and 14.9 MB lighter, the GPU process 5.1 and 6.5 heavier. The renders are `82e9748`'s but where runs differ: the fixtures' port in the address, the update row's clock, which tabs had loaded, a drag's and a slide's frame, a progress bar, and the time of day's sky.

## The interactions, with Pane in front

`interaction-timing` takes the front, so it ran apart, with the user's go (2026-11-10, `tests/ab.ts`, 5 rounds taking turns, every run passing, `evidence/views-migration/shell-interactions3/`). Each interval runs from the input to its frame's commit to Core Animation, where 0.19.2's end: the like-for-like point the budgets compare, which `tests/lib/signposts.ts` takes where the build names it (`commit=`).

| Interaction | 0.19.2 | Views | Budget | |
|---|---:|---:|---:|---|
| Tab switch, median (p90) | 12.7 (23.5) ms | 4.0 (12.1) ms | ≤ 0.19.2's | ✓ |
| ⌘T | 14.6 (25.1) ms | 7.4 (9.6) ms | ≤ 0.19.2's | ✓ |
| ⌘L | 13.8 (21.0) ms | 3.3 (7.8) ms | ≤ 0.19.2's | ✓ |
| Open the profile sheet | 40.6 (49.1) ms | 8.1 (30.5) ms | | |
| Choose a platform | 4.5 (13.2) ms | 1.7 (2.1) ms | | |

The Views build also timed a click into the address (3.3 ms), the switcher by Ctrl-Tab (6.2), and a sleeping tab woken to its first paint (45.1), which 0.19.2 doesn't mark.

## Found on the way

- **A render left its recording behind.** `pane/ui/render.cc` paints views on a pixel canvas, and Views keeps each view's last recording and replays it for a view its layer's next paint leaves alone; the window's compositor records in points (pixel canvases are ChromeOS's), so a view painted for a render could then draw at twice its size: with the sidebar's footer on a layer of its own, `views-sidebar`'s capture showed its New at 2×. The channel's `render`, a test copy's alone, now has every view it painted record again for the screen.
- **Quit Pane? re-rasters at every frame of its entrance,** on either raster: its card grows by a scale the UI thread steps, and cc rasters each new scale (68 to 72 raster tasks an entrance on the GPU, 512 to 608 on the CPU, 17 to 19 frames presented and none or one dropped on both). A scale the compositor drives, the plan's sampled curve, would raster it once. It now does (The first frames back).
- **Settings' cards paint a path across the panel,** so every tile under them rasters when Settings shows: 145 tiles, 11 to 17 ms on the CPU. Traced again, a showing rasters 44 tiles in 4 to 5 ms, about half of it clearing them and most of the rest the cards' paths; in pieces, 4.0 ms where it took 5.0 (The first frames back).
- **The capture `sidebar-resize` names `list-top` raced its reveal once:** the list still at the end it was scrolled to, in 1 of 11 runs of this work's builds and none of 5 of `3f62f15`'s; the scenario checks nothing in it.

## Rerunning

```bash
bun tests/ab.ts --base <0.19.2>/Contents/MacOS/Pane --candidate <Views build>/Contents/MacOS/Pane \
	--out <dir> --rounds 3 launch unlock pages memory scroll frame-latency
```

`benchmark-frame-latency` is the gate's measure, tracked now: `benchmark-scroll`'s scroll, and each of the page's frames from its compositor's submission to its presentation, with the stages under it. The rest of what this document cites (the memory dumps, cc's layer snapshots, the traced loads and launches) came from probes kept with the evidence, not in the tree.

The keys' timings come from the rig, in the background, on a build of each, taking turns; each interval prints `late=`'s reading (`medianMs`, `p90Ms`) and, where the build says them, `committed` and `presented`:

```bash
bun tests/run.ts --binary <Views build>/Contents/MacOS/Pane --out <dir> tests/rigs/views-key-timing.ts
```
