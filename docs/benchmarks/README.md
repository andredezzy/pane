# Benchmarks

Measured comparisons of Pane builds, each with its raw measurements beside it.

- [interactions.md](interactions.md) — how much sooner 0.13.1 answers a tab switch (−38%), a new tab (−45%), and the address field (−55%), from input to the frame, against the code before. Raw data: [interactions.json](interactions.json).
- [native-vs-electron.md](native-vs-electron.md) — how much the native app (0.11.2) improved on the last Electron release (0.6.0), its baseline: what improved, what did not, then size, launch, memory, idle CPU, page loads, Speedometer and JetStream, and the engines, each with its method, the native app first. Raw data: [native-vs-electron.json](native-vs-electron.json).

## Rerunning native-vs-electron

The benchmark is six scenarios of the scenario runner (`tests/scenarios/benchmark-*.ts`, on `tests/lib/benchmark.ts`), run on the native app as the run's binary, with the Electron app named by `PANE_BENCHMARK_ELECTRON`. Two Swift tools measure for them, the same for both apps: `tests/tools/window-wait.swift` (the first window on screen) and `tests/tools/process-usage.swift` (the kernel's CPU, wakeup, and energy counters per process). `tests/tools/benchmark-report.ts` gathers a run into one JSON file and prints the summary table, the native app first. Each app is used from a copy out of its release dmg, never from `/Applications`, and launched by its executable, so a Pane the user has open is never touched.

```bash
# 1. Both apps, copied out of their release dmgs, mounted read-only.
for app in electron:v0.6.0:0.6.0 native:v0.11.2:0.11.2; do
	IFS=: read name tag version <<< "$app"
	mkdir -p "/tmp/bench/dmg-$name" "/tmp/bench/mnt-$name" "/tmp/bench/$name"
	gh release download "$tag" -R andredezzy/pane -p "Pane-$version-arm64.dmg" -D "/tmp/bench/dmg-$name"
	diskutil image attach --readOnly --nobrowse --mountPoint "/tmp/bench/mnt-$name" "/tmp/bench/dmg-$name/Pane-$version-arm64.dmg"
	ditto "/tmp/bench/mnt-$name/Pane.app" "/tmp/bench/$name/Pane.app"
	diskutil eject "/tmp/bench/mnt-$name"
done

# 2. The benchmark: about 50 minutes, with both apps' windows on screen.
PANE_BENCHMARK_ELECTRON=/tmp/bench/electron/Pane.app/Contents/MacOS/Pane \
	bun tests/run.ts --binary /tmp/bench/native/Pane.app/Contents/MacOS/Pane \
	--out /tmp/bench/out --timeout 5400 \
	benchmark-bundle benchmark-launch benchmark-memory benchmark-cpu benchmark-pages benchmark-suites

# 3. The raw data in one file, and the summary table.
bun tests/tools/benchmark-report.ts /tmp/bench/out docs/benchmarks/native-vs-electron.json \
	/tmp/bench/dmg-native/Pane-0.11.2-arm64.dmg /tmp/bench/dmg-electron/Pane-0.6.0-arm64.dmg
```

Any one scenario can run alone; each writes `<scenario>.json` into `--out`, and the report takes whichever are there.

| Scenario | What it measures | Runs | Time |
| --- | --- | --- | --- |
| `benchmark-bundle` | Each bundle's size on disk and in bytes, its frameworks and binaries; Chromium's version and the user agent | 1 | seconds |
| `benchmark-launch` | From the exec: the first window on screen, the Electron app's UI, the first tab's page loaded; cold (new data) and warm (relaunch) | 7 each | 3 min |
| `benchmark-memory` | Every process's physical footprint, summed, and the process count, with 1, 5, and 10 tabs, 30 s idle | 3 | 10 min |
| `benchmark-cpu` | Idle CPU of every process over 60 s with one tab, 90 s after it loaded: CPU time, wakeups, and CPU energy from the kernel's counters | 5 | 26 min |
| `benchmark-pages` | Three local fixtures and three real sites: load, first and largest contentful paint (painted and presented), first byte; empty and filled cache | 3 | 4 min |
| `benchmark-suites` | Speedometer 3.1 and JetStream 3.0 from browserbench.org | 2 | 8 min |

How the two apps are kept equal and apart (`tests/lib/benchmark.ts`):

- Both get a throwaway data directory and home (`HOME` and `CFFIXED_USER_HOME`), `--use-mock-keychain`, a fixed `--remote-debugging-port`, `--no-first-run`, and the same throttling switches: `--disable-backgrounding-occluded-windows` and `--disable-renderer-backgrounding`. The two apps share the user's bundle ID, `com.pane.desktop`, so without the home of their own, URLSession's cache and cookies and Crashpad's database would be the user's.
- The Electron app's data is `@pane/veil` in its home, seeded with one profile, "Personal". The native app gets `PANE_DATA_DIR`, `PANE_UPDATE_URL` at the fixtures, and `PANE_ELECTRON_DATA_DIR` at an empty directory, as `tests/lib/pane.ts` gives it, and makes its own "Personal" profile when it opens its launch URL.
- Neither checks GitHub for updates: the Electron app's check has no switch, so `--host-resolver-rules` leaves `api.github.com` unresolved for both.
- Tabs open the way each app's UI opens them: the Electron app's tRPC `tabs.open`, sent from its UI page, and the native app's launch URL for the first tab and its automation channel (`openTab`) for the rest. The CPU scenario leaves the channel off, since it polls ten times a second.
- The apps take turns going first in each run, so a trend in the Mac's load falls on both. Each scenario records the load averages when it starts and ends.

Energy is measured only as the kernel's estimate of what each process spent on the CPU: `powermetrics`, which covers the GPU, the display, and DRAM, needs root.
