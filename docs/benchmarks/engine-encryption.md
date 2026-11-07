# Encrypting every profile file: what it costs

What the plan for ADR 0121 measured against: the published 0.15.2, from its release's dmg, against the release candidate, stripped as a release is, taking turns on this Mac (M5 Pro, 2026-11-05 and 06), the two alternating so a trend in its load falls on both.

| Measure | 0.15.2 | Candidate | Budget | Change |
|---|---|---|---|---|
| Launch to first window, warm (21 runs each) | 273 ms | 271 ms | ≤ +10 ms | -2.3 ms ✓ |
| Launch to first window, the first launch on new data (21 runs each) | 267 ms | 279 ms | ≤ +10 ms | +12.1 ms **over** |
| Launch to first page painted, warm | 572 ms | 575 ms |  | +3.0 ms |
| PIN to first page painted | 487 ms | 530 ms | ≤ +100 ms | +43 ms ✓ |
| Launch to first page painted, no PIN | 590 ms | 596 ms | ≤ +100 ms | +7 ms ✓ |
| Warm page load, smoke.html | 4.5 ms | 4.9 ms | ≤ +3% | +8.9% (+0.4 ms) |
| Warm page load, find.html | 4.5 ms | 4.5 ms | ≤ +3% | +0.0% ✓ |
| Warm page load, nav-a.html | 5.2 ms | 5.3 ms | ≤ +3% | +1.9% ✓ |
| Warm page load, en.wikipedia.org/wiki/Web_browser | 488 ms | 498 ms | ≤ +3% | +1.9% ✓ |
| Warm page load, www.apple.com/ | 470 ms | 330 ms | ≤ +3% | -29.8% ✓ |
| Warm page load, github.com/ | 463 ms | 463 ms | ≤ +3% | -0.0% ✓ |
| Repeat visit after a relaunch, en.wikipedia.org/wiki/Web_browser (32 runs each) | 483 ms | 465 ms | ≤ +5% | -3.7% ✓ |
| Repeat visit after a relaunch, www.apple.com/ (32 runs each) | 1374 ms | 1260 ms | ≤ +5% | -8.3% ✓ |
| Repeat visit after a relaunch, github.com/ (32 runs each) | 874 ms | 852 ms | ≤ +5% | -2.4% ✓ |
| History search, 5,000 entries, p95 | 12.0 ms | 11.9 ms | ≤ +10% | -0.8% ✓ |
| First request after relaunch, 3,000 cookies, p95 | 19.3 ms | 20.1 ms | ≤ +10% | +4.1% ✓ |
| Local Storage first read, p95 | 2.3 ms | 2.3 ms |  | +0.0% |
| IndexedDB: put / get 2,000 records of 1 KiB | 51 / 11 ms | 52 / 11 ms |  |  |
| IndexedDB: a blob of 8 MiB, put / get | 6 / 2 ms | 18 / 7 ms |  | +11 / +5 ms |
| Memory, 1 tab | 147 MB | 148 MB | ≤ +5% | +0.7% ✓ |
| Memory, 5 tabs | 238 MB | 238 MB | ≤ +5% | -0.1% ✓ |
| Memory, 10 tabs | 356 MB | 357 MB | ≤ +5% | +0.4% ✓ |
| One-time migration |  | 222–233 MB/s | reported; progress past 1 s | 659 MB in 2.9 s, panel shown |

What the table says:

- **The first launch on new data is the one measure over budget**, by 2 ms, once per install. The published 0.17.2 measures 269 ms there, so about 10 ms of it is this work's, from a cause not found. The user accepted it, 2026-11-06. A warm launch, which is every other one, is level.
- **The local fixtures load in under 6 ms,** where a tenth of a millisecond is a percent or more: `smoke.html` reads +8.9%, which is 0.4 ms.
- **Log reuse mattered.** The first candidate turned LevelDB's off for every store, and the Default profile's, which open at startup, cost a warm launch 15 ms. Off only where it must be, it is level: an encrypted file refuses an append, and LevelDB goes on without reusing.
- **A blob costs its chunks:** 8 MiB takes 11 ms more to write and 5 ms more to read. No budget names it.
- **A real site's repeat visit is noisy:** apple.com read +18.7% over 5 runs and -8.3% over 32, which the table reports.

As felt, from recordings of the window, timed frame by frame (three of each), to the last frame that changed: ⌃⌘L, 1783–1883 ms against 1817–1883 ms; ⌘R on an encyclopedia's page, 1983–2067 ms against 2000–2050 ms.

## Rerunning

```bash
bun tests/run.ts --binary <copy of Pane>/Contents/MacOS/Pane --out <dir> --timeout 2400 \
	tests/benchmarks/benchmark-launch.ts tests/benchmarks/benchmark-unlock.ts tests/benchmarks/benchmark-pages.ts \
	tests/benchmarks/benchmark-memory.ts tests/benchmarks/benchmark-stores.ts
```

The benchmarks measure the run's binary alone, so two builds take turns. `benchmark-stores` measures what a store's format costs, on data big enough to reach the disk: 5,000 history entries searched through `chrome.history`, the first request after a relaunch with 3,000 cookies to load, IndexedDB's writes and reads, Local Storage's first read, and the repeat visit after a relaunch; `PANE_STORES_RELAUNCHES` sets how many relaunches.
