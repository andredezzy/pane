# Electron 0.6.0 against native 0.11.2

Pane's last Electron release, 0.6.0, against the native app at 0.11.2, on one Mac, with the same tools and the same method for both: size, the engines, launch, memory, idle CPU, page loads, and Speedometer and JetStream. ADR 0004 moved Pane to the native shell; ADRs 0051 and 0088 retired the Electron app at 0.7.0. The `benchmark-*` scenarios measured everything here, [README.md](README.md) reruns them, and [electron-vs-native.json](electron-vs-native.json) holds every measurement. Numbers are medians, with the runs' range in brackets. MB is 2^20 bytes, as du and footprint count it.

## Summary

| Metric | Unit | Better | Electron 0.6.0 | Native 0.11.2 | Native vs Electron |
| --- | --- | --- | ---: | ---: | ---: |
| Download (dmg) | MB | lower | 115 | 179 | +55% (1.55×) |
| App on disk | MB | lower | 314 | 395 | +26% (1.26×) |
| Engine framework on disk | MB | lower | 262 | 381 | +45% (1.45×) |
| Cold launch: window on screen | ms | lower | 377 [329–406] | 334 [301–417] | −11% (0.89×) |
| Cold launch: first page loaded | ms | lower | 429 [380–452] | 431 [394–514] | ±0% (1.00×) |
| Warm launch: window on screen | ms | lower | 388 [351–395] | 351 [320–372] | −9% (0.91×) |
| Warm launch: first page loaded | ms | lower | 434 [405–444] | 347 [328–364] | −20% (0.80×) |
| Memory, 1 tab | MB | lower | 244 [243–244] | 144 [143–146] | −41% (0.59×) |
| Memory, 5 tabs | MB | lower | 295 [294–295] | 230 [229–233] | −22% (0.78×) |
| Memory, 10 tabs | MB | lower | 401 [401–403] | 345 [344–345] | −14% (0.86×) |
| Processes, 1 tab |  | lower | 6 | 8 | +33% (1.33×) |
| Processes, 10 tabs |  | lower | 15 | 17 | +13% (1.13×) |
| Idle CPU, one tab | % of a core | lower | 0.05 [0.03–0.18] | 0.16 [0.05–0.18] | +203% (3.03×) |
| Idle wakeups, one tab | per s | lower | 0.07 [0.00–0.28] | 0.03 [0.00–0.95] | −50% (0.50×) |
| Idle CPU energy, one tab | mW | lower | 0.39 [0.16–3.01] | 0.72 [0.45–1.17] | +86% (1.86×) |
| smoke.html, empty cache: largest contentful paint | ms | lower | 28 | 16 | −43% (0.57×) |
| find.html, empty cache: largest contentful paint | ms | lower | 28 | 24 [24–28] | −14% (0.86×) |
| nav-a.html, empty cache: largest contentful paint | ms | lower | 28 [24–28] | 28 | ±0% (1.00×) |
| en.wikipedia.org/wiki/Web_browser, empty cache: largest contentful paint | ms | lower | 716 [704–944] | 744 [740–756] | +4% (1.04×) |
| apple.com, empty cache: largest contentful paint | ms | lower | 1300 [1288–1536] | 1344 [1320–1348] | +3% (1.03×) |
| github.com, empty cache: largest contentful paint | ms | lower | 1376 [1284–1400] | 1408 [1388–1464] | +2% (1.02×) |
| Speedometer 3.1 | score | higher | 51.1 [50.9–51.4] | 46.0 [46.0–46.1] | −10% (0.90×) |
| JetStream 3.0 | score | higher | 397 [395–399] | 435 [431–439] | +10% (1.10×) |

The two apps run different engines: Electron 0.6.0 is Electron 41.4.0, on Chromium 146.0.7680.216, and native 0.11.2 is on Chromium 157.0.8103.15, eleven milestones later. Every difference here is the shell's and the engine's together; [Caveats](#caveats) says where that matters.

In short: the native app is 55% bigger to download and 26% bigger on disk. It puts its window on screen about 40 ms sooner and, on a relaunch, its first page 87 ms sooner. It uses 100 MB (41%) less memory with one tab, a gap that narrows to 56 MB (14%) at ten. Both idle at under 0.2% of one core, with ranges that overlap. On the real sites the two are within 4% of each other on largest contentful paint, and which one is ahead changed between two passes an hour apart. JetStream scores 10% higher on the native app and Speedometer 10% lower.

## Machine, builds, and conditions

- Mac17,9, Apple M5 Pro (18 cores), 48 GB, macOS 27.0.1 (26A434).
- 2026-10-30, 02:27 to 03:49 UTC (23:27 to 00:49 in the Mac's time zone, UTC−3).
- The Mac was in use while the benchmark ran, with other apps open and used: the one-minute load average at the scenarios' starts and ends ranged from 2.29 to 5.32, and WindowServer was at about half a core when top sampled it before the runs. The numbers carry that noise. The apps took turns going first in each run, so a trend in the load falls on both.
- The builds are the published `Pane-0.6.0-arm64.dmg` (120,965,338 bytes, SHA-256 `669f1d74…1966`) and `Pane-0.11.2-arm64.dmg` (187,503,474 bytes, `5e42e1d1…19c0`), both matching GitHub's digests. Each was mounted read-only, its app copied into `/tmp/bench`, and launched by its executable, never from `/Applications`. Both apps' bundle ID is `com.pane.desktop`.
- Both apps got the same switches in every scenario: `--use-mock-keychain`, `--no-first-run`, `--no-default-browser-check`, a fixed `--remote-debugging-port` (CDP reads every page), `--disable-backgrounding-occluded-windows` and `--disable-renderer-backgrounding` (in use, another app's window can cover the benchmark's, and Chromium would throttle it), and `--host-resolver-rules=MAP api.github.com ~NOTFOUND` (the Electron app checks GitHub for updates at launch and has no switch to stop it).
- Each launch had its own data directory and home (`HOME` and `CFFIXED_USER_HOME`). The Electron app keeps its data in that home, at `@pane/veil`, seeded with one profile, "Personal". The native app gets `PANE_DATA_DIR`, `PANE_UPDATE_URL` at the fixtures, and `PANE_ELECTRON_DATA_DIR` at an empty directory, as `tests/lib/pane.ts` gives it, and creates its own "Personal" profile when it opens its launch URL.
- Tabs open the way each app's UI opens them. The Electron app gets its tRPC `tabs.open`, sent from its UI page. The native app gets its launch URL for the first tab, and its automation channel's `openTab` for the rest.

## Size

`du -sk` gives the bytes on disk (allocated blocks); the sum of the files' sizes, links not followed, gives the apparent bytes (in the JSON, within 0.3% of the bytes on disk). The dmg is the published file.

| | Electron 0.6.0 | Native 0.11.2 | Native vs Electron |
| --- | ---: | ---: | ---: |
| dmg | 115.4 MB | 178.8 MB | +55% |
| App on disk | 313.9 MB | 395.4 MB | +26% |
| Engine framework | 261.7 MB (`Electron Framework.framework`) | 380.8 MB (`PaneCore.framework`) | +45% |
| Its binary | 171.0 MB | 258.8 MB | +51% |
| The app's own code | `app.asar`, 50.3 MB (below), and a 52 KB executable | a 6.0 MB Swift executable | |
| The app's resources | 50.4 MB, `app.asar` included | 8.5 MB | |
| Other frameworks and helpers | Squirrel, Mantle, ReactiveObjC (0.8 MB); four helper apps (0.9 MB) | none outside the framework | |

Inside the frameworks (du): PaneCore has its binary (258.8 MB), Resources (86.0 MB, of which 52.7 MB of locales and a 19.9 MB `resources.pak`), Helpers (20.0 MB, of which two notification ("Alerts") helper apps of 7.8 MB each and Crashpad's 2.7 MB handler), and Libraries (15.8 MB, nearly all SwiftShader). Electron Framework has its binary (171.0 MB), Resources (64.7 MB, of which 46.9 MB of locales and a 6.5 MB `resources.pak`), Libraries (24.8 MB: SwiftShader 16.0 MB, ANGLE's `libGLESv2` 6.6 MB, FFmpeg 2.1 MB), and Helpers (1.2 MB).

The Electron app's `app.asar` holds its built main process and UI (2.8 MB, `out/`) and the `node_modules` it ships beside them, unbundled: 24.4 MB of source maps, 16.7 MB of JavaScript, and 3.8 MB of TypeScript sources and declarations, with `lucide-react` alone at 23.6 MB.

The native app is the bigger one because its engine is: `PaneCore.framework` is Chromium's `chrome/` layer (ADR 0009), with its extension system and Chrome's other features, where Electron Framework is Chromium's content layer with Node.js and Electron's own code. Its binary alone is 87.8 MB larger, more than the 81.5 MB between the two apps.

## Engines

Read over CDP (`Browser.getVersion`) and from the first tab (`navigator.userAgent`, `navigator.userAgentData.getHighEntropyValues`).

| | Electron 0.6.0 | Native 0.11.2 |
| --- | --- | --- |
| Chromium | 146.0.7680.216 | 157.0.8103.15 |
| V8 | 14.6.202.34 | 15.7.112.9 |
| Electron | 41.4.0 | — |
| Built as | Electron's own release build | `engine/args/release.gn`: `is_debug = false`, and no `is_official_build`, the switch Chromium's own releases are built with |
| `navigator.userAgent` | `… Chrome/146.0.7680.216 Safari/537.36` | `… Chrome/157.0.0.0 Safari/537.36` |
| `fullVersionList` | Not-A.Brand 24.0.0.0, Chromium 146.0.7680.216 | Chromium 157.0.8103.15, Not-A.Brand 99.0.0.0 |

The Electron app's user agent carries Chromium's full version (the app strips only Electron's token and its own); the native app's is Chrome's reduced one, with the full version in the Client Hints.

## Launch

The harness reads the clock just before it spawns the app's executable, and times three moments from it:

- **Window on screen.** `tests/tools/window-wait.swift`, the same tool for both apps, polls the window list (`CGWindowListCopyWindowInfo`) every 2 ms. The time is when the list first shows an on-screen, normal-layer window owned by the app's process; the list before it, read 2.5 to 9.2 ms earlier, showed none.
- **The Electron app's UI.** Its UI is a page of its own, so the harness reads that page's load, its first contentful paint, and the moment its sidebar lists the profile (a MutationObserver in the page).
- **The first page.** The first tab's page is the local fixture `smoke.html`: its load event's end and first contentful paint, from the page's own Performance API (`performance.timeOrigin` plus the entry's time). The native app opens it as its launch URL. The Electron app takes no URL, so the harness asks for the tab over its tRPC channel the moment its sidebar lists the profile. "Tab requested" is when that request left, 7 to 14 ms after the profile was listed: the harness's own round trips.

Cold is a first launch on new data. Warm is the relaunch on the data the cold launch left, after 3 s to write it and a quit. Neither is cold for the OS: the apps' files stay in the file cache between launches, and `purge` needs root. There were 7 runs of each, the apps taking turns. All times are in ms from the exec.

| From the exec to | Electron cold | Native cold | Electron warm | Native warm |
| --- | ---: | ---: | ---: | ---: |
| Window on screen | 377 [329–406] | 334 [301–417] | 388 [351–395] | 351 [320–372] |
| UI page loaded | 348 [302–372] | — | 350 [322–363] | — |
| Sidebar lists the profile | 364 [317–388] | — | 366 [337–379] | — |
| UI first contentful paint | 439 [396–467] | — | 443 [413–460] | — |
| Tab requested | 374 [325–397] | — | 379 [347–388] | — |
| First page: load event end | 429 [380–452] | 431 [394–514] | 434 [405–444] | 347 [328–364] |
| First page: first contentful paint | 456 [404–476] | 464 [428–550] | 461 [429–479] | 382 [352–415] |

The native app's window is on screen 43 ms sooner cold and 37 ms sooner warm, at the median; the cold ranges overlap. The Electron app shows its window when its UI page finishes loading (`window.ts`, on `did-finish-load`), and the window list shows it 27 to 39 ms after that page's load event. The UI's first contentful paint then lands 54 to 68 ms after the window is on screen, in every run: for that long, the window shows the placeholder colour the app gives it until the page's CSS lands. For the native app, the window list says when the window is on screen, not what it shows; when AppKit draws the sidebar was not timed.

The first page loads at the same time cold (429 against 431 ms). Warm, the native app loads it 87 ms (20%) sooner and paints it 79 ms sooner: its relaunch is 84 ms faster than its first launch, while the Electron app's is no faster than its own.

## Memory

`footprint`, macOS's own tool, reads every process of the app: the browser, its GPU, network, storage, and renderer helpers, and Crashpad's handlers, found by their executable inside the app's bundle whatever their parent (Crashpad's handler is launchd's child). Each process's physical footprint is what Activity Monitor's Memory column shows, and their sum is the figure below. footprint's own total for the set, which counts memory the processes share once, is within 2% of the sum; summed resident memory, which counts shared pages in every process, is in the JSON.

The tabs are local fixtures (`smoke.html`, `find.html`, `nav-a.html`, in turn, each with its own query), opened as the app's UI opens a tab, with the last one shown. Each count is read 30 s after its last tab loaded. There were 3 runs, each a launch on new data going from 1 to 5 to 10 tabs. Small pages make what grows with the tabs the browser's and the app's cost per tab, not a site's.

| Tabs | Electron | Native | Native vs Electron | Electron processes | Native processes |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 243.6 [243.3–243.9] | 143.9 [142.6–146.4] | −99.7 MB (−41%) | 6 | 8 |
| 5 | 294.6 [293.8–295.2] | 230.4 [229.0–233.1] | −64.2 MB (−22%) | 10 | 12 |
| 10 | 401.1 [400.8–402.7] | 345.3 [344.3–345.3] | −55.8 MB (−14%) | 15 | 17 |

By process, in MB (the median of the runs), with the number of processes where there are several:

| Process | Electron, 1 tab | Native, 1 tab | Electron, 5 | Native, 5 | Electron, 10 | Native, 10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Browser (the app's main process) | 60.3 | 64.5 | 59.7 | 67.3 | 64.4 | 73.7 |
| GPU | 94.4 | 26.3 | 77.0 | 44.4 | 94.9 | 67.3 |
| Renderers | 79.8 (3) | 29.4 (2) | 148.8 (7) | 95.1 (6) | 232.3 (12) | 178.7 (11) |
| Network service | 8.8 | 11.8 | 9.1 | 12.1 | 9.6 | 12.7 |
| Storage service | — | 6.4 | — | 6.7 | — | 7.1 |
| Crashpad handlers | — | 5.5 (2) | — | 5.5 (2) | — | 5.5 (2) |

With one tab, most of the 100 MB is the Electron app's GPU process (68 MB more) and its renderers (50 MB more). The Electron app has two renderers beyond its tabs, its UI page and its overlay page (the tab switcher's `surface` window). The native app has one beyond its tab, with no page target, and its UI lives in its browser process, which is 4 MB larger than the Electron app's. The native app has more processes: Crashpad's two handlers and a storage service, neither of which the Electron app runs as a process.

Each added tab costs the same in renderers in both apps (16.9 MB in Electron's, 16.6 MB in the native app's, from 1 to 10 tabs). The native app's GPU process, though, grows by 4.6 MB a tab (26.3, 44.4, 67.3 MB), where the Electron app's does not (94.4, 77.0, 94.9 MB), and its browser process grows by 1.0 MB a tab against 0.5. So the whole app grows by 22.4 MB a tab against 17.5, and the gap narrows from 100 MB to 56 MB. Past 10 tabs was not measured.

## Idle CPU

One tab on `smoke.html`, nothing happening, and every process of the app summed over one minute, starting 90 s after the page loaded. `tests/tools/process-usage.swift` reads the kernel's own counters for each process every second (`proc_pid_rusage`): CPU time to the nanosecond, idle and interrupt wakeups, and the kernel's estimate of the energy each process spent on the CPU. The native app runs without its automation channel here, which polls ten times a second. There were 5 runs, the apps taking turns.

| Per second, over the minute | Electron | Native |
| --- | ---: | ---: |
| CPU, % of one core | 0.051 [0.027–0.182] | 0.156 [0.054–0.182] |
| Interrupt wakeups | 3.23 [3.10–6.68] | 4.21 [3.81–11.43] |
| Idle wakeups | 0.07 [0.00–0.28] | 0.03 [0.00–0.95] |
| CPU energy, mW | 0.39 [0.16–3.01] | 0.72 [0.45–1.17] |

The runs: Electron 0.090, 0.027, 0.029, 0.182, 0.051%; native 0.171, 0.156, 0.054, 0.057, 0.182%.

Both apps idle at under 0.2% of one core in every run, and under 3.1 mW. The median favours the Electron app, but the ranges overlap: the two quietest native minutes (0.054 and 0.057%) sit with the Electron app's, and its busiest (0.182%) matches the Electron app's busiest. What moves a minute is short bursts of Chromium's own work. Both apps burst about two minutes after launch, in every run (0.4 to 1.4% of a core for a second), and the native app also does about 111 s after launch (1.1 to 1.6%). Beyond the bursts, the native app's two busiest minutes had its network service (0.081% of a core) and then its GPU process (0.082%) working through part of the minute. The Electron app's busiest had a three-second burst in its browser and GPU processes, up to 6.2% of a core.

Two earlier passes were replaced by this one. One read ps and top, which round CPU to 10 ms and 0.1%, too coarse for an idle app. The other read the kernel's counters 30 s after launch, and caught Chromium's work at about 30 s and 60 s after startup in both apps; it is in the JSON under `earlierPasses`.

## Page loads

Each run is a launch on new data with one tab. The tab goes to each page in turn through CDP's `Page.navigate`, as if typed into the address bar, with `about:blank` between pages. The page's Navigation Timing and Paint Timing are read 2 s after its load event, since largest contentful paint can come after it. The first pass loads every page with an empty cache; the second, in the same launch, loads them again with what the first cached. The pages are three local fixtures from `tests/pages`, served by the runner on 127.0.0.1, and three real sites, over the Mac's own network. There were 3 runs. All times are in ms from navigation start.

Empty cache:

| Page | Load, Electron | Load, native | LCP, Electron | LCP, native | First byte, Electron | First byte, native |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `smoke.html` | 5 | 7 [5–7] | 28 | 16 | 1 | 1 |
| `find.html` | 6 [5–8] | 7 [7–8] | 28 | 24 [24–28] | 1 [1–2] | 1 |
| `nav-a.html` | 6 [5–9] | 8 [8–9] | 28 [24–28] | 28 | 1 [1–2] | 1 |
| en.wikipedia.org/wiki/Web_browser | 1499 [1205–2485] | 1155 [1127–1167] | 716 [704–944] | 744 [740–756] | 349 [345–574] | 336 [336–337] |
| www.apple.com | 2468 [2176–2510] | 2236 [2223–2236] | 1300 [1288–1536] | 1344 [1320–1348] | 598 [518–775] | 541 [536–547] |
| github.com | 1440 [1403–1508] | 1414 [1393–1448] | 1376 [1284–1400] | 1408 [1388–1464] | 496 [329–500] | 434 [390–502] |

Filled cache:

| Page | Load, Electron | Load, native | LCP, Electron | LCP, native | First byte, Electron | First byte, native |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `smoke.html` | 50 [48–51] | 5 [5–6] | 76 [72–76] | 16 | 1 [1–2] | 1 |
| `find.html` | 6 [5–8] | 4 | 28 [28–36] | 20 [16–20] | 1 [1–2] | 1 |
| `nav-a.html` | 5 [5–7] | 6 [6–9] | 28 | 20 [20–28] | 1 | 1 |
| en.wikipedia.org/wiki/Web_browser | 504 [474–1214] | 505 [496–515] | 200 [192–1116] | 164 [164–188] | 116 [111–635] | 111 [109–111] |
| www.apple.com | 737 [424–750] | 732 [729–748] | 576 [240–592] | 592 [272–608] | 133 [1–140] | 132 [1–147] |
| github.com | 409 [397–420] | 502 [494–650] | 496 [484–512] | 552 [544–720] | 113 [112–115] | 113 [110–195] |

On the local fixtures, the load event ends within 2 ms in both apps, but the first frame reaches the screen later in the Electron app. Paint Timing splits the two. The renderer paints sooner in the Electron app (5.6 to 12.2 ms after navigation start with an empty cache, against 10.5 to 16.7 ms), and the painted frame then takes 15.7 to 27.1 ms to be on screen, against 3.7 to 13.6 ms in the native app, over both passes. (A paint entry's start time is its `presentationTime` in both engines, as checked on a page in each, so the paints compare like for like.) In every run, the Electron app's filled-cache `smoke.html`, the first page after the empty-cache pass, took 50 ms to its load event against 5. The earlier pass showed the same.

On the real sites, the two apps' largest contentful paints are within 4% of each other, and which app is ahead does not hold from one pass to the next. An earlier pass of the same scenario, an hour before, had the Electron app behind on Wikipedia's largest contentful paint (848 against 756 ms) where this one has it ahead (716 against 744). The native app's runs are the tighter ones: its range never passed 120 ms on an empty cache, where the Electron app's reached 1,280 ms (Wikipedia's load event). From the renderer's paint to the screen takes 19 to 48 ms in the Electron app and 19 to 65 ms in the native app on these sites.

## Speedometer and JetStream

Speedometer 3.1 and JetStream 3.0, from browserbench.org, run in the app's first tab, in its window, on screen. Each is started once loaded, the way its Start button starts it (`benchmarkClient.start()`, `JetStream.start()`), and read once done: Speedometer's score and its ± over 10 iterations, and JetStream's overall score, the geometric mean of its 77 tests. There were 2 runs per app, each a launch on new data, the apps taking turns, with the throttling switches above. The tab's viewport is each app's default window: 1052×741 in the Electron app, 1244×890 in the native app, both above Speedometer's 850×650 minimum.

| | Electron 0.6.0 | Native 0.11.2 | Native vs Electron |
| --- | ---: | ---: | ---: |
| Speedometer 3.1 | 51.4 ± 4.9, 50.9 ± 4.5 | 46.0 ± 4.1, 46.1 ± 4.3 | −10% |
| Its iterations after the first | 48.0 to 56.1 | 46.9 to 48.7 | |
| JetStream 3.0 | 399.25, 395.14 | 430.82, 439.28 | +10% |
| Time to run | Speedometer 40 to 45 s, JetStream 54 to 63 s | Speedometer 40 to 45 s, JetStream 51 s | |

JetStream, JavaScript and WebAssembly, scores 10% higher on the native app. Speedometer, which drives web apps through the DOM and layout, scores 10% lower on it. The Electron app scores higher in each of the ten iterations, in both runs: the first is both apps' lowest (32.9 and 33.5 against 29.8 and 29.1), the second is the closest (48.0 and 48.6 against 47.4 and 46.9), and the rest run 52.3 to 56.1 against 47.0 to 48.7. The apps run different Chromium and V8 versions, built differently, in different viewports. These numbers do not say how much of either difference is the shell's.

## Caveats

- **Two engines.** Chromium 146 against 157, V8 14.6 against 15.7, and two builds: Electron's release build, and Pane's `engine/args/release.gn`, which sets no `is_official_build`. Wherever the work is the engine's, a difference can be the engine's as much as the shell's: JetStream, Speedometer, a page's own loading, a renderer's memory. Telling the two apart would take the same benchmarks on a stock Chromium 157 built from `release.gn` and on Electron 41.4.0 alone, which were not run here; the `Chromium.app` in the local engine checkout is a 156 build from an earlier sync.
- **Two architectures.** The native app's UI is AppKit and SwiftUI, in its browser process. The Electron app's is two web pages in renderers of their own: its UI, and the overlay its tab switcher draws in. Those are its two extra renderers, and its window waits for the UI page to load. What each costs is in the launch and memory sections, and comes from the shells as much as the engines.
- **Different windows.** Each app opened its own default window, so the native app's tab viewport is 42% larger in area (1244×890 against 1052×741). Rendering-bound work, such as Speedometer's, ran on more pixels there.
- **A Mac in use.** The load average ran from 2.29 to 5.32, not a quiet machine. Memory and the browser benchmarks hardly moved from run to run (memory within 4 MB, Speedometer within 0.5 points, JetStream within 2%); launch times (ranges up to 122 ms wide), idle CPU, and the real sites are where the noise shows.
- **Ways of opening tabs.** The native app opens its first tab from its launch URL and the rest through its automation channel; the Electron app opens them all through tRPC from its UI page. The Electron app's first page at launch includes the harness's request, 7 to 14 ms after its sidebar listed the profile.
- **Reruns.** The first page-load pass stopped when one Electron launch never served DevTools; no crash report was written, and the rerun passed. The idle CPU pass was rerun with a longer wait and the kernel's counters (above). The page-load pass reported here added Paint Timing's paint time; the one before it is in the JSON under `earlierPasses`.
- **Not measured.**
  - Energy beyond the kernel's CPU estimate: `powermetrics`, which covers the GPU, the display, and DRAM, needs root.
  - A launch cold for the OS: `purge` needs root.
  - The native UI's first draw, frame times, scrolling, and input latency.
  - More than one profile, extensions, proxies, and fingerprints.
  - Memory with real sites, or past 10 tabs.
  - Tab sleep: both apps sleep a tab after 15 minutes and unload a profile after 30, and every measurement ran before either.
  - The Electron app's x64, Windows, and Linux builds.
  - Other Macs and other macOS versions.
