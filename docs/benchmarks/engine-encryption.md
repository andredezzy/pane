# Encrypting every profile file: the baseline

What `docs/plans/engine-file-encryption.md` measures against: the published 0.15.2, from its release's dmg, on this Mac (M5 Pro, in use), 2026-11-05. Phase 6 runs the same scenarios on the candidate, and adds its column here with the budgets.

| Measure | 0.15.2 |
|---|---|
| Launch to first window, warm (median of 7) | 269 ms |
| Launch to first page painted, warm | 553 ms |
| PIN to first page painted (median of 5) | 491 ms |
| Launch to first page painted, no PIN | 591 ms |
| Memory, 1 tab(s) (footprint sum) | 146 MB |
| Memory, 5 tab(s) (footprint sum) | 237 MB |
| Memory, 10 tab(s) (footprint sum) | 354 MB |
| Page load, cold, smoke.html | 8 ms |
| Page load, cold, find.html | 7 ms |
| Page load, cold, nav-a.html | 8 ms |
| Page load, cold, https://en.wikipedia.org/wiki/Web_browser | 1038 ms |
| Page load, cold, https://www.apple.com/ | 2382 ms |
| Page load, cold, https://github.com/ | 1722 ms |
| Page load, warm, smoke.html | 4 ms |
| Page load, warm, find.html | 4 ms |
| Page load, warm, nav-a.html | 6 ms |
| Page load, warm, https://en.wikipedia.org/wiki/Web_browser | 498 ms |
| Page load, warm, https://www.apple.com/ | 744 ms |
| Page load, warm, https://github.com/ | 463 ms |
| History search, 5,000 entries, median / p95 | 9.7 / 11.8 ms |
| First request after relaunch, 3,000 cookies, median / p95 | 17.4 / 18.2 ms |
| Local Storage first read, 2,000 items, median / p95 | 2.1 / 2.2 ms |
| IndexedDB: put / get 2,000 records of 1 KiB | 50 / 11 ms |
| IndexedDB: blob of 8 MiB, put / get | 7 / 3 ms |
| Repeat visit after a relaunch, https://en.wikipedia.org/wiki/Web_browser | 434 ms (first visit 1997) |
| Repeat visit after a relaunch, https://www.apple.com/ | 1611 ms (first visit 2351) |
| Repeat visit after a relaunch, https://github.com/ | 852 ms (first visit 1446) |

## Rerunning

```bash
bun tests/run.ts --binary <copy of Pane>/Contents/MacOS/Pane --out <dir> --timeout 2400 \
	benchmark-launch benchmark-unlock benchmark-pages benchmark-memory benchmark-stores
```

Without `PANE_BENCHMARK_ELECTRON`, the benchmarks measure the run's binary alone. `benchmark-stores` measures what a store's format costs, on data big enough to reach the disk: 5,000 history entries searched through `chrome.history`, the first request after a relaunch with 3,000 cookies to load, IndexedDB's writes and reads, Local Storage's first read, and the repeat visit after a relaunch.
