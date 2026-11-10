# Pane's views raster on the CPU

Pane's own views, the window's sidebar, toolbar, panels, and dialogs, raster on the CPU, in the browser's raster thread; the GPU composites them, as it does everything, and pages still raster on the GPU. `//pane` turns off Chromium's `UiGpuRasterization` feature (`pane/app/engine_switches.cc`), the seam Chromium keeps for drawing its UI where GPU raster is blocklisted. It was taken at the end of the shell (`docs/plans/views-migration.md`), on its budgets.

## Why

- **Memory.** Rastered on the GPU, which in Chromium 158 is Skia Graphite on Dawn, the Views window kept the GPU process 12.5 MB fuller than 0.19.2's at rest with one tab, and the total 3.0% over 0.19.2's against a budget of 1%: about 8 MB of it was GPU rasterization's own, the pipelines, the worker threads that compile them, and Skia's caches, which no layout of the window removes. The window's tiles, the rest, were cut first (ADR 0073). On the CPU the GPU process holds 7.2 MB less, and the total is 1.7% under 0.19.2's at one tab and 2.4% at ten (`docs/benchmarks/views-shell.md`).
- **A cold launch.** Graphite compiles the window's pipelines before its first frame, on Dawn's worker threads: traced from the start, a cold launch presented the window's first frame 32 ms later than on the CPU, and launch to the first window read 9.7 ms over 0.19.2's, where on the CPU it is 13.8 ms under, and a warm one 18.7.
- **The screen draws what a render paints.** `pane/ui/render.cc` paints views on the CPU, and the renders are what each surface was compared against, the Swift's renders and Paper. On the GPU, paths antialias otherwise: an icon's edges differed from the render by up to 47 of 255. On the CPU the toolbar is within 5 of its render.
- **No surface waits on a pipeline the first time it shows.** On the GPU, a draw the window hasn't made yet compiles its pipeline first: Quit Pane?, its shadow cast by a rounded rect, dropped 19 frames at its first showing. On the CPU it dropped none.
- AppKit drew the Swift app's UI on the CPU too.

## What it costs

A surface rasters on the one CPU thread the browser's compositor has when it first shows. From the input to the frame on the screen, medians, the GPU's first: Settings 12.3 and 21.6 ms, Quit Pane? 12.8 and 20.7, the App icon picker 22.3 and 27.0, a Theme change 22.5 and 23.8. A tab switch (10.5 and 9.2), ⌘T (27.3 and 22.0), ⌘L (9.7 and 9.3), and the tab switcher (8.5 and 9.2) are level or faster.

Two shadows are cast by a rect rounded at 0.57 of their radius, as a layer's corners are (ADR 0057): Quit Pane?'s and the App icon picker's (a Gaussian of 32) and the tab switcher's (of 10). Skia blurs a rounded rect once, a corner's worth, where it works a path's blur out again for each tile it rasters: Quit Pane? took 37.8 ms to its first frame with the squircle's shadow. Blurred that wide, the two shapes' shadows are within a 255th. Under GPU raster the rounded rect would have cost a new shader at the first showing, so it comes with this decision.

## Rejected

- **GPU raster, with the window's tiles cut.** It holds memory at ten tabs (+0.1%) and the browser process, and not at one (+3.0%), and a cold launch's first window came 9.7 ms later than 0.19.2's.
- **Smaller UI tiles, or a smaller tile budget,** to keep GPU raster: the UI compositor's tile sizes and memory policy have no seam short of a Chromium patch.
- **Keeping Settings and the dialogs rastered while hidden,** so their first frame waits on nothing: it would hold their tiles in memory the whole time.

## How it's checked

The shell's budgets, rerun by `tests/ab.ts` against the last Swift release (`docs/benchmarks/views-shell.md`); `views-key-timing` and `views-settings`'s timing for the surfaces' first frames; a window's capture against its render.

What would win back the first frames: Quit Pane? re-rasters at every frame of its entrance, since its card grows by a scale the UI thread steps and cc rasters each new scale (a compositor-driven scale, the plan's sampled curve, would end that under either raster); Settings' cards paint a path across the panel, so every tile under them rasters.
