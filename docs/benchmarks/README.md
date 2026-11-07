# Benchmarks

Measured comparisons of Pane builds, each with its raw measurements beside it.

- [interactions.md](interactions.md) — how much sooner 0.13.1 answers a tab switch (−38%), a new tab (−45%), and the address field (−55%), from input to the frame, against the code before. Raw data: [interactions.json](interactions.json).
- [native-vs-electron.md](native-vs-electron.md) — how much the native app (0.11.2) improved on the last Electron release (0.6.0), its baseline: what improved, what did not, then size, launch, memory, idle CPU, page loads, Speedometer and JetStream, and the engines, each with its method, the native app first. Raw data: [native-vs-electron.json](native-vs-electron.json).

- [engine-encryption.md](engine-encryption.md) — what encrypting every profile file costs, against 0.15.2, by the budgets of `docs/plans/engine-file-encryption.md`.

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
