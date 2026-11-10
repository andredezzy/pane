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

## Found on the way

- **A render left its recording behind.** `pane/ui/render.cc` paints views on a pixel canvas, and Views keeps each view's last recording and replays it for a view its layer's next paint leaves alone; the window's compositor records in points (pixel canvases are ChromeOS's), so a view painted for a render could then draw at twice its size: with the sidebar's footer on a layer of its own, `views-sidebar`'s capture showed its New at 2×. The channel's `render`, a test copy's alone, now has every view it painted record again for the screen.
- **Quit Pane? re-rasters at every frame of its entrance,** on either raster: its card grows by a scale the UI thread steps, and cc rasters each new scale (68 to 72 raster tasks an entrance on the GPU, 512 to 608 on the CPU, 17 to 19 frames presented and none or one dropped on both). A scale the compositor drives, the plan's sampled curve, would raster it once.
- **Settings' cards paint a path across the panel,** so every tile under them rasters when Settings shows: 145 tiles, 11 to 17 ms on the CPU.
- **The capture `sidebar-resize` names `list-top` raced its reveal once:** the list still at the end it was scrolled to, in 1 of 11 runs of this work's builds and none of 5 of `3f62f15`'s; the scenario checks nothing in it.

## Rerunning

```bash
bun tests/ab.ts --base <0.19.2>/Contents/MacOS/Pane --candidate <Views build>/Contents/MacOS/Pane \
	--out <dir> --rounds 3 launch unlock pages memory scroll frame-latency
```

`benchmark-frame-latency` is the gate's measure, tracked now: `benchmark-scroll`'s scroll, and each of the page's frames from its compositor's submission to its presentation, with the stages under it. The rest of what this document cites (the memory dumps, cc's layer snapshots, the traced loads and launches) came from probes kept with the evidence, not in the tree.
