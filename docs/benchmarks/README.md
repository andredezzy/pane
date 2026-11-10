# Benchmarks

Measured comparisons of Pane builds, each with its raw measurements beside it.

- [interactions.md](interactions.md) — how much sooner 0.13.1 answers a tab switch (−38%), a new tab (−45%), and the address field (−55%), from input to the frame, against the code before. Raw data: [interactions.json](interactions.json).
- [native-vs-electron.md](native-vs-electron.md) — how much the native app (0.11.2) improved on the last Electron release (0.6.0), its baseline: what improved, what did not, then size, launch, memory, idle CPU, page loads, Speedometer and JetStream, and the engines, each with its method, the native app first. Raw data: [native-vs-electron.json](native-vs-electron.json).

- [pgo-vs-0.19.1.json](pgo-vs-0.19.1.json) — the raw medians behind "0.19.2's PGO engine against the published 0.19.1" below.
- [engine-encryption.md](engine-encryption.md) — what encrypting every profile file costs, against 0.15.2, by the budgets of the plan behind ADR 0121.
- [views-shell.md](views-shell.md) — the Views build at the end of the shell against 0.19.2, by the budgets the plan sets there: launch, unlock, warm loads, memory at 1 and 10 tabs, scroll, and a painted frame's way to the screen; what the GPU process held for the window, what was cut, and why Pane's views raster on the CPU (ADR 0123). Raw data: [views-shell.json](views-shell.json).

## 0.19.2's PGO engine against the published 0.19.1

The same Pane on an engine built with Chrome's PGO profile for its branch (`chrome_pgo_phase = 2`, no official build), against the published 0.19.1. Each stripped, taking turns over three rounds of every benchmark on an M5 Pro on 2026-11-08, then five rounds of unlock alone, since the first three put PIN to page 32 ms apart. Medians. Raw data: [pgo-vs-0.19.1.json](pgo-vs-0.19.1.json).

| Measure | 0.19.1 | PGO | Change |
|---|---:|---:|---:|
| Launch to first window, cold | 278.6 ms | 283.6 ms | +5.0 ms |
| Launch to first window, warm | 289.9 ms | 284.4 ms | −5.4 ms |
| PIN to first page painted (five rounds) | 474.8 ms | 485.0 ms | +10.2 ms |
| No PIN, launch to first page painted (five rounds) | 613.8 ms | 619.8 ms | +6.0 ms |
| Cold load, Apple, first paint | 332 ms | 260 ms | −21.7% |
| Cold load, GitHub | 707.6 ms | 630.5 ms | −10.9% |
| Cold load, Wikipedia, first paint | 268 ms | 264 ms | −1.5% |
| Cold load, `nav-a.html` | 7.2 ms | 6.1 ms | −15.3% |
| Warm load, `find.html`, first paint | 24 ms | 20 ms | −16.7% |
| Warm load, GitHub | 360.0 ms | 370.1 ms | +2.8% |
| Warm load, Apple | 560.7 ms | 617.2 ms | +10.1%, its first byte 51 ms later |
| History query, p95 | 11.1 ms | 10.7 ms | −3.6% |
| First request with 3,000 cookies, p95 | 18.5 ms | 19.1 ms | +3.2% |
| Memory, 1 tab | 159.0 MB | 156.6 MB | −1.5% |
| Memory, 10 tabs | 370.8 MB | 359.0 MB | −3.2% |
| GPU process, 10 tabs | 71.2 MB | 60.8 MB | −14.5% |
| Browser process, 10 tabs | 85.8 MB | 85.7 MB | −0.1% |
| Scroll, frames dropped of 480 | 0 | 0 | 0 |
| The framework, stripped | 274.2 MB | 260.6 MB | −5.0% |

**What PGO buys is page work.** First paint and load fall wherever the page's own work dominates: 10 to 22% on Apple's and GitHub's cold loads, and a millisecond or so on the fixtures, which are 5 to 17% faster. Warm loads of real sites move with the network: Apple's warm load came 51 ms later to its first byte, before any of the engine's work. Memory falls 1.5 to 3.2%, most of it the GPU process; Pane's own browser process is unchanged.

**Launch and unlock are the system's.** Launch moves 5 ms either way, as it does between rounds. PIN to page, 32 ms apart over three rounds, is 10 ms over five, 8.6 of them the volume's attach (`hdiutil`), which the engine has no part in; one build's rounds spread over 72 ms.

A build from the scratchpad started no volume watchdog: run from the repository, as the published one is, it starts it as 0.19.1 does, so the first memory runs counted one process fewer on that side, the watchdog's 0.1 MB.

## 0.19.0's candidate against the published 0.18.0

The simplifications and Chromium 158, with every upgrade path kept (ADR 0122), against the budgets they were held to. Each from its dmg, stripped, taking turns over three rounds on an M5 Pro on 2026-11-07, so a trend in the Mac's load falls on both. Medians.

| Measure | 0.18.0 | Candidate | Change | Budget | |
|---|---:|---:|---:|---:|---|
| Launch to first window, warm | 271.0 ms | 265.7 ms | −5.3 ms | ≤ +5 ms | ✓ |
| Launch to first window, cold | 271.6 ms | 270.6 ms | −1.0 ms | ≤ +5 ms | ✓ |
| PIN to first page painted | 496.3 ms | 498.8 ms | +2.5 ms | ≤ +25 ms | ✓ |
| No PIN, launch to first page painted | 577.2 ms | 570.2 ms | −7.0 ms | | |
| History query, p95 | 11.4 ms | 11.0 ms | −3.5% | ≤ +3% | ✓ |
| First request with 3,000 cookies, p95 | 18.9 ms | 19.2 ms | +1.6% | ≤ +3% | ✓ |
| Repeat visit after a relaunch, Wikipedia | 100.7 ms | 99.9 ms | −0.8% | ≤ +3% | ✓ |
| Repeat visit, Apple | 951.0 ms | 939.6 ms | −1.2% | ≤ +3% | ✓ |
| Repeat visit, GitHub | 352.8 ms | 368.7 ms | +4.5% | ≤ +3% | over, within the site's noise |
| Warm load, Wikipedia | 157.7 ms | 158.7 ms | +0.6% | ≤ +3% | ✓ |
| Warm load, GitHub | 353.4 ms | 349.0 ms | −1.2% | ≤ +3% | ✓ |
| Warm load, Apple | 570.9 ms | 471.1 ms | −17.5% | ≤ +3% | ✓ |
| Warm load, `smoke.html` | 4.8 ms | 4.8 ms | 0 | ≤ +3% | ✓ |
| Warm load, `find.html` | 4.6 ms | 7.0 ms | +2.4 ms | ≤ +3% | **over, accepted** |
| Warm load, `nav-a.html` | 5.3 ms | 8.3 ms | +3.0 ms | ≤ +3% | **over, accepted** |
| Memory, 1 tab | 138.8 MB | 140.7 MB | +1.4% | ≤ +1% | **over, accepted** |
| Memory, 5 tabs | 226.9 MB | 229.3 MB | +1.0% | ≤ +1% | ✓ |
| Memory, 10 tabs | 338.7 MB | 342.5 MB | +1.1% | ≤ +1% | **over, accepted** |

The user accepted the misses marked so, as the cost of Chromium 158 (2026-11-07).

**Memory is Chromium 158's.** On the previous candidate, measured the same way, the GPU process at ten tabs grew 1.8 MB and the renderers 4 MB, where the browser, Pane's own process, grew 0.5 MB. Against the same code on 157 the GPU process is the same 1.8 MB up and the renderers 1.6 MB. Sets of rounds differ from one another by about 1%: an earlier set read +1.8 to +2.1%.

**A tiny page's slower load is Chromium 158's too.** A fixture's load comes in two modes, about 4.6 ms and 7.7 ms, the 3 ms between the first byte and `DOMContentLoaded`, with its first paint a frame later. 157 builds took the slower mode in 2 to 5 of 27 loads, and 158 builds in 12 to 18 of 27, once in none of 18. It doesn't show on a real site, whose noise is far larger.

**A real site's load is the site's.** Wikipedia comes in three modes on every build, about 105, 160, and 250 ms, by the first byte's time, so a median moves with how many of each a run drew.

**The upgrade paths cost nothing at launch:** with them back, launch reads as it did without them, within a set's noise.

**The volume's watchdog cost an unlock 250 ms, and doesn't now.** From 0.18.1 it took the data's lock as Pane ended, and held it through an `hdiutil info` that found nothing. A Pane started in those 250 ms found the lock taken, and attached its image only once the key opened. `benchmark-unlock` relaunches at once, so it showed: 494 ms to 783 ms from the PIN. A quit that detached now ends the watchdog (ADR 0113).

## Rerunning

The benchmarks are scenarios that measure and never judge, in `tests/benchmarks/`, on `tests/lib/benchmark.ts`. The runner takes each by its path. Each writes `<name>.json` into `--out`, with no judgement in it: compare the same run on two builds, from copies out of their dmgs, never from `/Applications`.

```bash
bun tests/run.ts --binary <copy of Pane>/Contents/MacOS/Pane --out <dir> --timeout 2400 \
	tests/benchmarks/benchmark-launch.ts tests/benchmarks/benchmark-unlock.ts tests/benchmarks/benchmark-pages.ts \
	tests/benchmarks/benchmark-memory.ts tests/benchmarks/benchmark-stores.ts
```

| Benchmark | What it measures | Runs | Time |
| --- | --- | --- | --- |
| `benchmark-launch` | From the exec: the first window on screen and the first tab's page loaded; cold (new data) and warm (relaunch) | 7 each | 2 min |
| `benchmark-unlock` | From the PIN, and from a launch with none, to the first page painted, and the volume's attach within it | 5 each | 1 min |
| `benchmark-pages` | Three local fixtures and three real sites: load, first and largest contentful paint, first byte; empty and filled cache | 3 | 2 min |
| `benchmark-memory` | Every process's physical footprint, summed, and the process count, with 1, 5, and 10 tabs, 30 s idle | 3 | 5 min |
| `benchmark-stores` | History search, the first request with 3,000 cookies to load, IndexedDB, Local Storage's first read, and the repeat visit after a relaunch (`PANE_STORES_RELAUNCHES`, 5) | 5 | 1 min |
| `benchmark-scroll` | A steady wheel scroll down a long page: the page's frames presented, in part, or dropped (cc's `PipelineReporter`) | 3 × 3 | 1 min |
| `benchmark-frame-latency` | The same scroll: each of the page's frames from its compositor's submission to its presentation, and the stages under it | 3 × 3 | 1 min |
| `interaction-timing` | The app's own signposted intervals, from an input to the frame it commits | 8 rounds | 1 min |

`native-vs-electron.md` can't be rerun from this tree: its bundle, CPU, and suites benchmarks, its report, and the Electron app's half of `tests/lib/benchmark.ts` are gone. Commit `1a8b786` is the last able to rerun it, with its recipe in that commit's copy of this file.
