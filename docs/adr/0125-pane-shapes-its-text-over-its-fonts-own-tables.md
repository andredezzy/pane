# Pane's views shape their text over their fonts' own tables

The HarfBuzz faces that shape the text of Pane's views read each font table where CoreText keeps it, the font's own bytes, not a copy in the browser's heap: `//pane` turns on Chromium's `HarfBuzzZeroCopyFontTable` feature (`pane/app/engine_switches.cc`), off by default in Chromium 158 and a field trial for the Mac and Windows in Chromium's testing config (`testing/variations/fieldtrial_testing_config.json`, which Pane's build leaves out, ADR 0022). It was taken on the memory budget at one tab, for the first Views release (`docs/benchmarks/views-vs-swift.md`, Memory at one tab).

## Why

- **Memory.** gfx gives each typeface a HarfBuzz face of its own and, with the feature off, copies every table the face reads into the heap (`GetFontTable`, `ui/gfx/harfbuzz_font_skia.cc`). The sizes and weights of the system font Pane's views draw make many typefaces of it: at launch, with one tab, eleven held a copy each of SF's GPOS (408 KB) and GDEF (270 KB), 7.8 MB of the browser's 32 MB of live heap in a sampled heap profile, and each new typeface a surface draws later adds 0.68 MB. The Swift app's text went through CoreText, which reads the font's own tables. With the feature on, the browser's allocator holds what 0.19.2's does (38.0 MB against 38.0, footprint's App-Specific Tag 14), and the browser process is 8.1 MB lighter than with the tables copied. Memory at one tab went from 2.5% over 0.19.2's, against a budget of 1%, to 2.2% under it (146.1 against 149.4 MB, three rounds by `tests/ab.ts`), and at ten tabs to 1.8% under.
- **Nothing changes on screen.** The bytes HarfBuzz reads are the same, so the text shapes and draws the same: `surfaces`' 49 renders, light and dark, match K2b's of the same tree with the tables copied, pixel for pixel, but where any two runs differ (the update row's clock, the fixtures' port, the overlay scroll bar's fade, a spinner).
- **No frame waits on it.** A typeface's first face no longer copies its tables, and nothing else changes: each surface's first showing after a launch, from its input to its frame, was level with the feature off and on (Settings 39.1 and 39.8 ms, the App icon picker 31.8 and 31.6, Quit Pane? 15.4 and 13.1, find 13.8 and 11.4, the switcher 9.8 and 10.2, over nine launches each), and so was launch to the first window.
- It is a seam Chromium already has (ADR 0004): no patch.

## Rejected

- **One typeface for every size of the system font,** so its tables would be copied once: SF changes its outlines with its optical size, so the text would draw otherwise than Paper's boards and the Swift's, and the copy would still be the browser's.
- **One HarfBuzz face for every instance of a font,** in gfx: a Chromium patch, where a feature does it.
- **Making the surfaces made at launch at their first showing** (Settings, the Lock, the profile sheet, the extensions menus, Quit Pane?, find, the switcher), which allocate 2.7 MB of the browser's live heap but would give the footprint back far less, and make each first showing slower: the profile sheet, measured, gave back 0.1 to 0.2 MB of its 0.89 for a first showing 5.3 ms later to its commit. The user's call (`docs/benchmarks/views-vs-swift.md`).

## How it's checked

`EngineSwitchesTest` (`pane/app/engine_switches_unittest.cc`, `pane_unittests`): every launch, the user's and a scenario's, turns the feature on, beside `UiGpuRasterization` off. The memory budget, rerun by `tests/ab.ts` against the last Swift release; the browser process's allocator by footprint's categories and its live heap by Chromium's sampling heap profiler, from a test copy (the probe kept with the evidence, `evidence/views-migration/mem-1tab-probe`).
