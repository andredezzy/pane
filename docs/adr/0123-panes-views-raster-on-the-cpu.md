# Pane's views raster on the CPU

Pane's own views, the window's sidebar, toolbar, panels, and dialogs, raster on the CPU, in the browser's raster thread; the GPU composites them, as it does everything, and pages still raster on the GPU. `//pane` turns off Chromium's `UiGpuRasterization` feature (`pane/app/engine_switches.cc`), the seam Chromium keeps for drawing its UI where GPU raster is blocklisted. It was taken at the end of the shell (`docs/plans/views-migration.md`), on its budgets.

## Why

- **Memory.** Rastered on the GPU, which in Chromium 158 is Skia Graphite on Dawn, the Views window kept the GPU process 12.5 MB fuller than 0.19.2's at rest with one tab, and the total 3.0% over 0.19.2's against a budget of 1%: about 8 MB of it was GPU rasterization's own, the pipelines, the worker threads that compile them, and Skia's caches, which no layout of the window removes. The window's tiles, the rest, were cut first (ADR 0073). On the CPU the GPU process holds 7.2 MB less, and the total is 1.7% under 0.19.2's at one tab and 2.4% at ten (`docs/benchmarks/views-shell.md`).
- **A cold launch.** Graphite compiles the window's pipelines before its first frame, on Dawn's worker threads: traced from the start, a cold launch presented the window's first frame 32 ms later than on the CPU, and launch to the first window read 9.7 ms over 0.19.2's, where on the CPU it is 13.8 ms under, and a warm one 18.7.
- **The screen draws what a render paints.** `pane/ui/render.cc` paints views on the CPU, and the renders are what each surface was compared against, the Swift's renders and Paper. On the GPU, paths antialias otherwise: an icon's edges differed from the render by up to 47 of 255. On the CPU the toolbar is within 5 of its render.
- **No surface waits on a pipeline the first time it shows.** On the GPU, a draw the window hasn't made yet compiles its pipeline first: Quit Pane?, its shadow cast by a rounded rect, dropped 19 frames at its first showing. On the CPU it dropped none.
- AppKit drew the Swift app's UI on the CPU too.

## What it costs

A surface rasters on the one CPU thread the browser's compositor has when it first shows, and its frame shows a refresh later once the work from its BeginFrame to Core Animation outgrows one refresh, 8.33 ms at 120 Hz: on the GPU that work took about 6 ms, and on the CPU at first 10 to 11, the 8 to 9 ms that Settings, Quit Pane?, and the App icon picker lost. From the input to the frame on the screen, medians, the GPU's first: Settings 12.3 and 21.6 ms, Quit Pane? 12.8 and 20.7, the App icon picker 22.3 and 27.0, a Theme change 22.5 and 23.8. A tab switch (10.5 and 9.2), ⌘T (27.3 and 22.0), ⌘L (9.7 and 9.3), and the tab switcher (8.5 and 9.2) were level or faster.

Five changes in `//pane` won back Quit Pane?'s and the App icon picker's, and part of Settings' (`docs/benchmarks/views-shell.md`, The first frames back):

- **Pane's labels keep their laid-out text while hidden.** Views drops it at every hide, so each showing shaped its labels again on the UI thread before its first frame: Settings' paint took 2.0 ms, now 0.3, and the App icon picker's ↩ went through 41 fallback fonts at each showing, 2 ms.
- **A dialog grows and fades on the compositor's own animation** (`CompositorMotion`, the plan's compositor-thread path, running Pane's curves exactly, its first frame a frame in as `LayerMotion`'s), so cc rasters its card once, at its end's scale, where a scale the UI thread stepped was rastered again at every frame: Quit Pane?'s entrance took 64 to 96 raster tasks, now 32.
- **The scrim and the card fade apart,** as the Swift's `modal()` faded them. Faded as one group, they took the display compositor a pass the size of the window at each frame: the first frame's draw took 1.2 to 2.8 ms, now 0.4 to 0.7.
- **The card paints in its shadow's layer,** one layer's tiles where two rastered the card's middle twice: Quit Pane? 32 tiles, now 24; the picker 46 to 49, now 34 to 36.
- **Settings' cards and a dialog's card paint their squircle in pieces,** rectangles between the corners and each corner a path of its own, which Skia covers pixel for pixel as the whole path at 2× (`squircle_unittest.cc`), so a tile inside a card rasters no path: Settings' raster took 5.0 ms, now 4.0.

Now, from the input to the frame on the screen, medians, in the background on a Mac in use, taking turns with the build before these changes (eight runs each of `views-settings`, three of `views-key-timing`): Settings 20.4 → 16.0 ms, Quit Pane? 15.6 → 11.8, the App icon picker 26.8 → 17.7, a Theme change 22.5 → 22.7. The picker was faster in all eight pairs of runs and Settings in six. A Theme change was level, as were a tab switch (10.0 → 10.6), ⌘T (25.3 → 23.6), ⌘L (7.2 → 8.4), and the switcher (8.4 → 8.5), within what a run's median moves from one run to the next (Settings 12.4 to 23.9 ms on the same build).

What stays: Settings' first frame rasters 44 tiles in about 4 ms on the one raster thread, which content makes (`VizProcessTransportFactory`) with no seam for more, and so still misses the GPU's 12.3 ms; a tile whose memory the GPU last held clears in up to 110 µs, where a warm one takes 10. A Theme change waits mostly on Chromium's own work before its frame, every view's update for the new Theme (2.3 to 5.4 ms) and a new color provider built for it (1.5 ms), then rasters the window's contents again, and stays level with the GPU's.

The shell's budgets hold with the five: against 0.19.2, memory is 2.6% under at one tab and 1.8% at ten, and launch to the first window 16.8 ms sooner cold and 26.7 warm.

Two shadows are cast by a rect rounded at 0.57 of their radius, as a layer's corners are (ADR 0057): Quit Pane?'s and the App icon picker's (a Gaussian of 32) and the tab switcher's (of 10). Skia blurs a rounded rect once, a corner's worth, where it works a path's blur out again for each tile it rasters: Quit Pane? took 37.8 ms to its first frame with the squircle's shadow. Blurred that wide, the two shapes' shadows are within a 255th. Under GPU raster the rounded rect would have cost a new shader at the first showing, so it comes with this decision.

## Rejected

- **GPU raster, with the window's tiles cut.** It holds memory at ten tabs (+0.1%) and the browser process, and not at one (+3.0%), and a cold launch's first window came 9.7 ms later than 0.19.2's.
- **Smaller UI tiles, or a smaller tile budget,** to keep GPU raster: the UI compositor's tile sizes and memory policy have no seam short of a Chromium patch.
- **Keeping Settings and the dialogs rastered while hidden,** so their first frame waits on nothing: it would hold their tiles in memory the whole time.
- **A dialog's shadow as a nine-patch layer,** which the compositor draws with no raster: a 32-point Gaussian at 2× makes the nine-patch's image 835 pixels square, 2.8 MB, and its texture as much again, past the memory the shell's budgets leave.
- **Chromium's `ThemeChangeOptimization`,** off by default in Chrome: it moves a Theme change's work into tasks of their own, and timed to the frame that shows the new Theme, it saved nothing (21.0 and 21.0 ms, against 21.7 and 19.8).
- **The raster thread at the UI thread's priority,** set by a task of Pane's on it: its tasks ran 10% faster for Settings and no faster for Quit Pane?, within the runs' own swing, for retuning a thread content owns.
- **A card's one path clipped to its corners, or to bands across them,** in place of the pieces: Skia covers a clipped curve otherwise than the whole path, by up to 32/255.
- **The Dock's new icon a beat after a change** (0.1 s), so the 1.45 ms of the UI thread that giving the Dock its image takes, which lands ahead of a Theme's first frame as often as after it, stays clear of that frame: timed against the same build without it, a Theme change read 18.4 ms against 17.9, no gain, for a Dock a beat behind the window.

## How it's checked

The shell's budgets, rerun by `tests/ab.ts` against the last Swift release (`docs/benchmarks/views-shell.md`); `views-key-timing` and `views-settings`'s timing for the surfaces' first frames; a window's capture against its render; `squircle_unittest.cc` for the pieces against the whole path; cc's snapshots of a dialog's entrance for its one tiling, at its end's scale, and its first frame a frame in.
