# Benchmarks

Measured comparisons of Pane builds, each with its raw measurements beside it.

- [interactions.md](interactions.md) — how much sooner 0.13.1 answers a tab switch (−38%), a new tab (−45%), and the address field (−55%), from input to the frame, against the code before. Raw data: [interactions.json](interactions.json).
- [native-vs-electron.md](native-vs-electron.md) — how much the native app (0.11.2) improved on the last Electron release (0.6.0), its baseline: what improved, what did not, then size, launch, memory, idle CPU, page loads, Speedometer and JetStream, and the engines, each with its method, the native app first. Raw data: [native-vs-electron.json](native-vs-electron.json).

- [engine-encryption.md](engine-encryption.md) — what encrypting every profile file costs, against 0.15.2, by the budgets of the plan behind ADR 0121.

## 0.19.0's candidate against the published 0.18.0

The floor, the simplifications, and Chromium 158, against the budgets they were held to. Each from its dmg, stripped, taking turns on an M5 Pro on 2026-11-07, so a trend in the Mac's load falls on both. Medians over three rounds, six for page loads.

| Measure | 0.18.0 | Candidate | Change | Budget | |
|---|---:|---:|---:|---:|---|
| Launch to first window, warm | 273.6 ms | 271.2 ms | −2.4 ms | ≤ +5 ms | ✓ a gain |
| Launch to first window, cold | 271.1 ms | 267.5 ms | −3.6 ms | ≤ +5 ms | ✓ a gain |
| PIN to first page painted | 498.9 ms | 502.9 ms | +4.0 ms | ≤ +25 ms | ✓ |
| No PIN, launch to first page painted | 572.0 ms | 578.3 ms | +6.3 ms | | |
| History query, p95 | 11.1 ms | 11.1 ms | 0 | ≤ +3% | ✓ |
| First request with 3,000 cookies, p95 | 18.2 ms | 18.7 ms | +2.7% | ≤ +3% | ✓ |
| Repeat visit after a relaunch, Wikipedia | 140 ms | 135 ms | −3.6% | ≤ +3% | ✓ |
| Repeat visit, GitHub | 425.0 ms | 425.7 ms | +0.2% | ≤ +3% | ✓ |
| Repeat visit, Apple | 939.6 ms | 979.7 ms | +4.3% | ≤ +3% | over, within the site's noise |
| Warm load, `smoke.html` | 4.9 ms | 5.0 ms | +2% | ≤ +3% | ✓ |
| Warm load, `find.html` | 4.5 ms | 7.5 ms | +3.0 ms | ≤ +3% | **over** |
| Warm load, `nav-a.html` | 5.2 ms | 6.9 ms | +1.7 ms | ≤ +3% | **over** |
| Warm load, GitHub | 407.8 ms | 425.0 ms | +4.2% | ≤ +3% | over, within the site's noise |
| Warm load, Apple | 669.1 ms | 609.6 ms | −8.9% | ≤ +3% | ✓ |
| Warm load, Wikipedia | 176.0 ms | 249.2 ms | see below | ≤ +3% | noise |
| Memory, 1 tab | 146.7 MB | 149.7 MB | +2.1% | ≤ +1% | **over** |
| Memory, 5 tabs | 233.2 MB | 237.3 MB | +1.8% | ≤ +1% | **over** |
| Memory, 10 tabs | 343.9 MB | 351.0 MB | +2.1% | ≤ +1% | **over** |

**Memory is Chromium 158's.** At ten tabs the GPU process grows 1.8 MB and the renderers 4 MB, where the browser, Pane's own process, grows 0.5 MB. Against the same code on 157 the GPU process is the same 1.8 MB up and the renderers 1.6 MB; the published 0.18.2, on 157, sits under 0.18.0.

**A tiny page's slower load is Chromium 158's too.** A fixture's load comes in two modes, 4.6 ms and 7.7 ms, the 3 ms between the first byte and `DOMContentLoaded`, with its first paint a frame later. 157 builds took the slower mode in 2 to 5 of 27 loads, and 158 builds, stripped or not, in 13 and 14 of 27, once in none of 18. It doesn't show on a real site, whose noise is far larger.

**A real site's load is the site's.** Wikipedia comes in three modes on every build, about 105, 160, and 250 ms, by the first byte's time, so a median moves with how many of each a run drew. Round by round, the candidate's ratio to 0.18.0 runs from 0.97 to 1.68 there, and from 0.53 to 1.03 on Apple.

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
| `interaction-timing` | The app's own signposted intervals, from an input to the frame it commits | 8 rounds | 1 min |

`native-vs-electron.md` can't be rerun from this tree: its bundle, CPU, and suites benchmarks, its report, and the Electron app's half of `tests/lib/benchmark.ts` are gone. Commit `1a8b786` is the last able to rerun it, with its recipe in that commit's copy of this file.
