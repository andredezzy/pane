# How much the native app improved on the Electron app

What moving Pane from Electron to its native shell (ADR 0004) bought, measured: the native app at 0.11.2 against the last Electron release, 0.6.0, on one Mac, with the same tools and the same method for both. The native app is the subject throughout and the Electron app its baseline: every table puts the native app first, and every change is the native app's. ADRs 0051 and 0088 retired the Electron app at 0.7.0. The `benchmark-*` scenarios measured everything here, [README.md](README.md) reruns them, and [native-vs-electron.json](native-vs-electron.json) holds every measurement. Numbers are medians, with the runs' range in brackets. MB is 2^20 bytes, as du and footprint count it.

The two apps run different engines: Chromium 157 in the native app, Chromium 146 (Electron 41.4.0) in the Electron one. Every change below is the shell's and the engine's together; [Caveats](#caveats) says where that matters.

## What improved

| | Native 0.11.2 | Electron 0.6.0 | Improvement |
| --- | ---: | ---: | --- |
| Memory, 1 tab | 144 MB | 244 MB | 99.7 MB less (−41%) |
| Memory, 5 tabs | 230 MB | 295 MB | 64.2 MB less (−22%) |
| Memory, 10 tabs | 345 MB | 401 MB | 55.8 MB less (−14%) |
| Relaunch: first page loaded | 347 ms | 434 ms | 86 ms sooner (−20%) |
| Relaunch: first page painted | 382 ms | 461 ms | 79 ms sooner (−17%) |
| Cold launch: window on screen | 334 ms | 377 ms | 43 ms sooner (−11%) |
| Relaunch: window on screen | 351 ms | 388 ms | 37 ms sooner (−9%) |
| A painted frame reaching the screen (local pages) | 3.7–13.6 ms | 15.7–27.1 ms | 12 to 13.5 ms sooner |
| Largest contentful paint, `smoke.html` | 16 ms | 28 ms | 12 ms sooner (−43%) |
| `smoke.html` from the cache, after the empty-cache pass: load event | 5 ms | 50 ms | 45 ms sooner (−90%) |
| JetStream 3.0 (JavaScript, WebAssembly) | 435 | 397 | 10% higher |
| Real sites, spread of three runs (empty cache) | under 120 ms | up to 1,280 ms | steadier |

- **Memory** is the largest gain: 100 MB less with one tab. The native app draws its UI in AppKit and SwiftUI, in its browser process, where the Electron app needs two renderers for it (its UI page and its tab switcher's overlay) and a GPU process 68 MB larger. The gain narrows as tabs open, since the native app's GPU process grows by 4.6 MB a tab and the Electron app's does not: 56 MB less at ten tabs.
- **Launch:** the native app's window is on screen about 40 ms sooner, and on a relaunch its first page loads 86 ms sooner. Its relaunch is 84 ms faster than its first launch; the Electron app's is no faster than its own. The native app has no web page to wait for, where the Electron app shows its window before its UI has painted: a plain placeholder colour for 54 to 68 ms in every run.
- **Frames reach the screen sooner.** On the local pages, a frame the renderer has painted is on screen 3.7 to 13.6 ms later in the native app, against 15.7 to 27.1 ms in the Electron app.
- **JetStream** scores 10% higher.
- **Real sites load as fast, and more steadily:** largest contentful paint is within 4% either way, but the native app's runs never spread more than 120 ms, where the Electron app's reached 1,280 ms.

## What did not improve

| | Native 0.11.2 | Electron 0.6.0 | Change |
| --- | ---: | ---: | --- |
| Download (dmg) | 179 MB | 115 MB | 55% bigger |
| App on disk | 395 MB | 314 MB | 26% bigger |
| Speedometer 3.1 (web apps: DOM, layout) | 46.0 | 51.1 | 10% lower |
| Cold launch: first page loaded | 431 ms | 429 ms | the same, within each other's range |
| Real sites, largest contentful paint | 744 / 1,344 / 1,408 ms | 716 / 1,300 / 1,376 ms | the same, within 4%: 2 to 4% later in this pass, and the order flipped between passes |
| Idle CPU, one tab | 0.16% of a core | 0.05% of a core | both under 0.2%; the runs overlap |
| Processes, 1 tab | 8 | 6 | 2 more (Crashpad's two handlers and a storage service) |

- **Size** is the cost of the engine: `PaneCore.framework` is Chromium's `chrome/` layer (ADR 0009), with its extension system and Chrome's other features, where Electron Framework is Chromium's content layer with Node.js. The native app's own code is 44 MB smaller, but its engine's binary alone is 88 MB larger.
- **Speedometer** is 10% lower. The engines, their builds (Pane's sets no `is_official_build`), and the windows (the native app's tab is 42% larger in area) all differ, and these runs cannot say how much of the gap is the shell's.
- **Idle CPU** is negligible in both, under 0.2% of one core in every run; the native app's median is higher, but the ranges overlap.

## Summary

Every metric measured, the native app first:

| Metric | Unit | Better | Native 0.11.2 | Electron 0.6.0 | Change |
| --- | --- | --- | ---: | ---: | --- |
| Download (dmg) | MB | lower | 179 | 115 | 63.5 MB bigger (+55%) |
| App on disk | MB | lower | 395 | 314 | 81.5 MB bigger (+26%) |
| Engine framework on disk | MB | lower | 381 | 262 | 119 MB bigger (+45%) |
| Cold launch: window on screen | ms | lower | 334 [301–417] | 377 [329–406] | 43 ms sooner (−11%) |
| Cold launch: first page loaded | ms | lower | 431 [394–514] | 429 [380–452] | the same (±0%) |
| Warm launch: window on screen | ms | lower | 351 [320–372] | 388 [351–395] | 37 ms sooner (−9%) |
| Warm launch: first page loaded | ms | lower | 347 [328–364] | 434 [405–444] | 86 ms sooner (−20%) |
| Memory, 1 tab | MB | lower | 144 [143–146] | 244 [243–244] | 99.7 MB less (−41%) |
| Memory, 5 tabs | MB | lower | 230 [229–233] | 295 [294–295] | 64.2 MB less (−22%) |
| Memory, 10 tabs | MB | lower | 345 [344–345] | 401 [401–403] | 55.8 MB less (−14%) |
| Processes, 1 tab |  | lower | 8 | 6 | 2 more (+33%) |
| Processes, 10 tabs |  | lower | 17 | 15 | 2 more (+13%) |
| Idle CPU, one tab | % of a core | lower | 0.16 [0.05–0.18] | 0.05 [0.03–0.18] | 0.10 more (+203%) |
| Idle wakeups, one tab | per s | lower | 0.03 [0.00–0.95] | 0.07 [0.00–0.28] | 0.03 fewer (−50%) |
| Idle CPU energy, one tab | mW | lower | 0.72 [0.45–1.17] | 0.39 [0.16–3.01] | 0.33 mW more (+86%) |
| smoke.html, empty cache: largest contentful paint | ms | lower | 16 | 28 | 12 ms sooner (−43%) |
| find.html, empty cache: largest contentful paint | ms | lower | 24 [24–28] | 28 | 4 ms sooner (−14%) |
| nav-a.html, empty cache: largest contentful paint | ms | lower | 28 | 28 [24–28] | the same (±0%) |
| en.wikipedia.org/wiki/Web_browser, empty cache: largest contentful paint | ms | lower | 744 [740–756] | 716 [704–944] | 28 ms later (+4%) |
| apple.com, empty cache: largest contentful paint | ms | lower | 1344 [1320–1348] | 1300 [1288–1536] | 44 ms later (+3%) |
| github.com, empty cache: largest contentful paint | ms | lower | 1408 [1388–1464] | 1376 [1284–1400] | 32 ms later (+2%) |
| Speedometer 3.1 | score | higher | 46.0 [46.0–46.1] | 51.1 [50.9–51.4] | 10% lower |
| JetStream 3.0 | score | higher | 435 [431–439] | 397 [395–399] | 10% higher |

## Machine, builds, and conditions

- Mac17,9, Apple M5 Pro (18 cores), 48 GB, macOS 27.0.1 (26A434).
- 2026-10-30, 02:27 to 03:49 UTC (23:27 to 00:49 in the Mac's time zone, UTC−3).
- The Mac was in use while the benchmark ran, with other apps open and used: the one-minute load average at the scenarios' starts and ends ranged from 2.29 to 5.32, and WindowServer was at about half a core when top sampled it before the runs. The numbers carry that noise. The apps took turns going first in each run, so a trend in the load falls on both.
- The builds are the published `Pane-0.11.2-arm64.dmg` (187,503,474 bytes, SHA-256 `5e42e1d1…19c0`) and `Pane-0.6.0-arm64.dmg` (120,965,338 bytes, `669f1d74…1966`), both matching GitHub's digests. Each was mounted read-only, its app copied into `/tmp/bench`, and launched by its executable, never from `/Applications`. Both apps' bundle ID is `com.pane.desktop`.
- Both apps got the same switches in every scenario: `--use-mock-keychain`, `--no-first-run`, `--no-default-browser-check`, a fixed `--remote-debugging-port` (CDP reads every page), `--disable-backgrounding-occluded-windows` and `--disable-renderer-backgrounding` (in use, another app's window can cover the benchmark's, and Chromium would throttle it), and `--host-resolver-rules=MAP api.github.com ~NOTFOUND` (the Electron app checks GitHub for updates at launch and has no switch to stop it).
- Each launch had its own data directory and home (`HOME` and `CFFIXED_USER_HOME`). The native app gets `PANE_DATA_DIR`, `PANE_UPDATE_URL` at the fixtures, and `PANE_ELECTRON_DATA_DIR` at an empty directory, as `tests/lib/pane.ts` gives it, and creates its "Personal" profile when it opens its launch URL. The Electron app keeps its data in that home, at `@pane/veil`, seeded with one profile, "Personal".
- Tabs open the way each app's UI opens them. The native app gets its launch URL for the first tab, and its automation channel's `openTab` for the rest. The Electron app gets its tRPC `tabs.open`, sent from its UI page.

## Size

`du -sk` gives the bytes on disk (allocated blocks); the sum of the files' sizes, links not followed, gives the apparent bytes (in the JSON, within 0.3% of the bytes on disk). The dmg is the published file.

| | Native 0.11.2 | Electron 0.6.0 | Change |
| --- | ---: | ---: | --- |
| The app's own code | a 6.0 MB Swift executable | `app.asar`, 50.3 MB (below), and a 52 KB executable | 44.3 MB smaller (−88%) |
| The app's resources | 8.5 MB | 50.4 MB, `app.asar` included | 41.9 MB smaller (−83%) |
| Other frameworks and helpers | none outside the framework | Squirrel, Mantle, ReactiveObjC (0.8 MB); four helper apps (0.9 MB) | 1.7 MB smaller |
| Engine framework | 380.8 MB (`PaneCore.framework`) | 261.7 MB (`Electron Framework.framework`) | 119.1 MB bigger (+45%) |
| Its binary | 258.8 MB | 171.0 MB | 87.8 MB bigger (+51%) |
| App on disk | 395.4 MB | 313.9 MB | 81.5 MB bigger (+26%) |
| dmg | 178.8 MB | 115.4 MB | 63.5 MB bigger (+55%) |

The native app's own code and resources are a fraction of the Electron app's: a 6.0 MB Swift executable and 8.5 MB of resources, against an `app.asar` of 50.3 MB. That archive holds the Electron app's built main process and UI (2.8 MB, `out/`) and the `node_modules` it ships beside them, unbundled: 24.4 MB of source maps, 16.7 MB of JavaScript, and 3.8 MB of TypeScript sources and declarations, with `lucide-react` alone at 23.6 MB.

What makes the native app bigger is its engine. `PaneCore.framework` is Chromium's `chrome/` layer (ADR 0009), with its extension system and Chrome's other features, where Electron Framework is Chromium's content layer with Node.js and Electron's own code. The framework's binary alone is 87.8 MB larger, more than the 81.5 MB between the two apps. Inside the frameworks (du): PaneCore has its binary (258.8 MB), Resources (86.0 MB, of which 52.7 MB of locales and a 19.9 MB `resources.pak`), Helpers (20.0 MB, of which two notification ("Alerts") helper apps of 7.8 MB each and Crashpad's 2.7 MB handler), and Libraries (15.8 MB, nearly all SwiftShader). Electron Framework has its binary (171.0 MB), Resources (64.7 MB, of which 46.9 MB of locales and a 6.5 MB `resources.pak`), Libraries (24.8 MB: SwiftShader 16.0 MB, ANGLE's `libGLESv2` 6.6 MB, FFmpeg 2.1 MB), and Helpers (1.2 MB).

## Engines

Read over CDP (`Browser.getVersion`) and from the first tab (`navigator.userAgent`, `navigator.userAgentData.getHighEntropyValues`).

| | Native 0.11.2 | Electron 0.6.0 |
| --- | --- | --- |
| Chromium | 157.0.8103.15 | 146.0.7680.216 |
| V8 | 15.7.112.9 | 14.6.202.34 |
| Electron | — | 41.4.0 |
| Built as | `engine/args/release.gn`: `is_debug = false`, and no `is_official_build`, the switch Chromium's own releases are built with | Electron's own release build |
| `navigator.userAgent` | `… Chrome/157.0.0.0 Safari/537.36` | `… Chrome/146.0.7680.216 Safari/537.36` |
| `fullVersionList` | Chromium 157.0.8103.15, Not-A.Brand 99.0.0.0 | Not-A.Brand 24.0.0.0, Chromium 146.0.7680.216 |

The native app is eleven Chromium milestones newer. Its user agent is Chrome's reduced one, with the full version in the Client Hints; the Electron app's carries Chromium's full version (the app strips only Electron's token and its own).

## Launch

The harness reads the clock just before it spawns the app's executable, and times three moments from it:

- **Window on screen.** `tests/tools/window-wait.swift`, the same tool for both apps, polls the window list (`CGWindowListCopyWindowInfo`) every 2 ms. The time is when the list first shows an on-screen, normal-layer window owned by the app's process; the list before it, read 2.5 to 9.2 ms earlier, showed none.
- **The first page.** The first tab's page is the local fixture `smoke.html`: its load event's end and first contentful paint, from the page's own Performance API (`performance.timeOrigin` plus the entry's time). The native app opens it as its launch URL. The Electron app takes no URL, so the harness asks for the tab over its tRPC channel the moment its sidebar lists the profile. "Tab requested" is when that request left, 7 to 14 ms after the profile was listed: the harness's own round trips.
- **The Electron app's UI.** Its UI is a page of its own, so the harness also reads that page's load, its first contentful paint, and the moment its sidebar lists the profile (a MutationObserver in the page).

Cold is a first launch on new data. Warm is the relaunch on the data the cold launch left, after 3 s to write it and a quit. Neither is cold for the OS: the apps' files stay in the file cache between launches, and `purge` needs root. There were 7 runs of each, the apps taking turns. All times are in ms from the exec.

| From the exec to | Native cold | Electron cold | Native warm | Electron warm |
| --- | ---: | ---: | ---: | ---: |
| Window on screen | 334 [301–417] | 377 [329–406] | 351 [320–372] | 388 [351–395] |
| First page: load event end | 431 [394–514] | 429 [380–452] | 347 [328–364] | 434 [405–444] |
| First page: first contentful paint | 464 [428–550] | 456 [404–476] | 382 [352–415] | 461 [429–479] |
| The Electron app's UI page loaded | — | 348 [302–372] | — | 350 [322–363] |
| Its sidebar lists the profile | — | 364 [317–388] | — | 366 [337–379] |
| Its UI's first contentful paint | — | 439 [396–467] | — | 443 [413–460] |
| Tab requested | — | 374 [325–397] | — | 379 [347–388] |

The native app's window is on screen 43 ms sooner cold and 37 ms sooner warm, at the median; the cold ranges overlap. The native app has no web page to load first. The Electron app shows its window when its UI page finishes loading (`window.ts`, on `did-finish-load`), and the window list shows it 27 to 39 ms after that page's load event. Its UI's first contentful paint then lands 54 to 68 ms after the window is on screen, in every run: for that long, its window shows the placeholder colour the app gives it until the page's CSS lands. For the native app, the window list says when the window is on screen, not what it shows; when AppKit draws the sidebar was not timed.

Relaunched, the native app loads its first page 86 ms (20%) sooner and paints it 79 ms sooner: its relaunch is 84 ms faster than its first launch, while the Electron app's relaunch is no faster than its own first launch. On a first launch, the first page loads at the same time in both (431 against 429 ms, within each other's range).

## Memory

`footprint`, macOS's own tool, reads every process of the app: the browser, its GPU, network, storage, and renderer helpers, and Crashpad's handlers, found by their executable inside the app's bundle whatever their parent (Crashpad's handler is launchd's child). Each process's physical footprint is what Activity Monitor's Memory column shows, and their sum is the figure below. footprint's own total for the set, which counts memory the processes share once, is within 2% of the sum; summed resident memory, which counts shared pages in every process, is in the JSON.

The tabs are local fixtures (`smoke.html`, `find.html`, `nav-a.html`, in turn, each with its own query), opened as the app's UI opens a tab, with the last one shown. Each count is read 30 s after its last tab loaded. There were 3 runs, each a launch on new data going from 1 to 5 to 10 tabs. Small pages make what grows with the tabs the browser's and the app's cost per tab, not a site's.

| Tabs | Native | Electron | Change | Native processes | Electron processes |
| ---: | ---: | ---: | --- | ---: | ---: |
| 1 | 143.9 [142.6–146.4] | 243.6 [243.3–243.9] | 99.7 MB less (−41%) | 8 | 6 |
| 5 | 230.4 [229.0–233.1] | 294.6 [293.8–295.2] | 64.2 MB less (−22%) | 12 | 10 |
| 10 | 345.3 [344.3–345.3] | 401.1 [400.8–402.7] | 55.8 MB less (−14%) | 17 | 15 |

By process, in MB (the median of the runs), with the number of processes where there are several:

| Process | Native, 1 tab | Electron, 1 tab | Native, 5 | Electron, 5 | Native, 10 | Electron, 10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Browser (the app's main process) | 64.5 | 60.3 | 67.3 | 59.7 | 73.7 | 64.4 |
| GPU | 26.3 | 94.4 | 44.4 | 77.0 | 67.3 | 94.9 |
| Renderers | 29.4 (2) | 79.8 (3) | 95.1 (6) | 148.8 (7) | 178.7 (11) | 232.3 (12) |
| Network service | 11.8 | 8.8 | 12.1 | 9.1 | 12.7 | 9.6 |
| Storage service | 6.4 | — | 6.7 | — | 7.1 | — |
| Crashpad handlers | 5.5 (2) | — | 5.5 (2) | — | 5.5 (2) | — |

With one tab, the native app uses 100 MB less, most of it in two places: its GPU process is 68 MB smaller, and its renderers 50 MB smaller. It has one renderer beyond its tab, with no page target, where the Electron app has two beyond its tabs, its UI page and its overlay page (the tab switcher's `surface` window). The native app's UI lives in its browser process instead, which is 4 MB larger than the Electron app's. Its other costs are small: Crashpad's two handlers (5.5 MB) and a storage service (6.4 MB), neither of which the Electron app runs as a process, are why it has two processes more, one renderer fewer.

Each added tab costs the same in renderers in both apps (16.6 MB in the native app's, 16.9 MB in the Electron app's, from 1 to 10 tabs). The native app's GPU process, though, grows by 4.6 MB a tab (26.3, 44.4, 67.3 MB), where the Electron app's does not (94.4, 77.0, 94.9 MB), and its browser process grows by 1.0 MB a tab against 0.5. So the native app grows by 22.4 MB a tab against 17.5, and its lead narrows from 100 MB at one tab to 56 MB at ten. Past 10 tabs was not measured.

## Idle CPU

One tab on `smoke.html`, nothing happening, and every process of the app summed over one minute, starting 90 s after the page loaded. `tests/tools/process-usage.swift` reads the kernel's own counters for each process every second (`proc_pid_rusage`): CPU time to the nanosecond, idle and interrupt wakeups, and the kernel's estimate of the energy each process spent on the CPU. The native app runs without its automation channel here, which polls ten times a second. There were 5 runs, the apps taking turns.

| Per second, over the minute | Native | Electron |
| --- | ---: | ---: |
| CPU, % of one core | 0.156 [0.054–0.182] | 0.051 [0.027–0.182] |
| Interrupt wakeups | 4.21 [3.81–11.43] | 3.23 [3.10–6.68] |
| Idle wakeups | 0.03 [0.00–0.95] | 0.07 [0.00–0.28] |
| CPU energy, mW | 0.72 [0.45–1.17] | 0.39 [0.16–3.01] |

The runs: native 0.171, 0.156, 0.054, 0.057, 0.182%; Electron 0.090, 0.027, 0.029, 0.182, 0.051%.

Idle CPU did not improve, and did not need to: both apps idle at under 0.2% of one core in every run, and under 3.1 mW. The native app's median is higher, but the ranges overlap: its two quietest minutes (0.054 and 0.057%) sit with the Electron app's, its busiest (0.182%) matches the Electron app's busiest, and none of its minutes drew more than 1.17 mW, where the Electron app's busiest drew 3.01. What moves a minute is short bursts of Chromium's own work. Both apps burst about two minutes after launch, in every run (0.4 to 1.4% of a core for a second), and the native app also does about 111 s after launch (1.1 to 1.6%). Beyond the bursts, the native app's two busiest minutes had its network service (0.081% of a core) and then its GPU process (0.082%) working through part of the minute. The Electron app's busiest had a three-second burst in its browser and GPU processes, up to 6.2% of a core.

Two earlier passes were replaced by this one. One read ps and top, which round CPU to 10 ms and 0.1%, too coarse for an idle app. The other read the kernel's counters 30 s after launch, and caught Chromium's work at about 30 s and 60 s after startup in both apps; it is in the JSON under `earlierPasses`.

## Page loads

Each run is a launch on new data with one tab. The tab goes to each page in turn through CDP's `Page.navigate`, as if typed into the address bar, with `about:blank` between pages. The page's Navigation Timing and Paint Timing are read 2 s after its load event, since largest contentful paint can come after it. The first pass loads every page with an empty cache; the second, in the same launch, loads them again with what the first cached. The pages are three local fixtures from `tests/pages`, served by the runner on 127.0.0.1, and three real sites, over the Mac's own network. There were 3 runs. All times are in ms from navigation start.

Empty cache:

| Page | Load, native | Load, Electron | LCP, native | LCP, Electron | First byte, native | First byte, Electron |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `smoke.html` | 7 [5–7] | 5 | 16 | 28 | 1 | 1 |
| `find.html` | 7 [7–8] | 6 [5–8] | 24 [24–28] | 28 | 1 | 1 [1–2] |
| `nav-a.html` | 8 [8–9] | 6 [5–9] | 28 | 28 [24–28] | 1 | 1 [1–2] |
| en.wikipedia.org/wiki/Web_browser | 1155 [1127–1167] | 1499 [1205–2485] | 744 [740–756] | 716 [704–944] | 336 [336–337] | 349 [345–574] |
| www.apple.com | 2236 [2223–2236] | 2468 [2176–2510] | 1344 [1320–1348] | 1300 [1288–1536] | 541 [536–547] | 598 [518–775] |
| github.com | 1414 [1393–1448] | 1440 [1403–1508] | 1408 [1388–1464] | 1376 [1284–1400] | 434 [390–502] | 496 [329–500] |

Filled cache:

| Page | Load, native | Load, Electron | LCP, native | LCP, Electron | First byte, native | First byte, Electron |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `smoke.html` | 5 [5–6] | 50 [48–51] | 16 | 76 [72–76] | 1 | 1 [1–2] |
| `find.html` | 4 | 6 [5–8] | 20 [16–20] | 28 [28–36] | 1 | 1 [1–2] |
| `nav-a.html` | 6 [6–9] | 5 [5–7] | 20 [20–28] | 28 | 1 | 1 |
| en.wikipedia.org/wiki/Web_browser | 505 [496–515] | 504 [474–1214] | 164 [164–188] | 200 [192–1116] | 111 [109–111] | 116 [111–635] |
| www.apple.com | 732 [729–748] | 737 [424–750] | 592 [272–608] | 576 [240–592] | 132 [1–147] | 133 [1–140] |
| github.com | 502 [494–650] | 409 [397–420] | 552 [544–720] | 496 [484–512] | 113 [110–195] | 113 [112–115] |

On the local fixtures, the native app's first frame reaches the screen sooner, though its load event ends at the same time, within 2 ms. Paint Timing says where the time goes. A frame the renderer has painted is on screen 3.7 to 13.6 ms later in the native app, against 15.7 to 27.1 ms in the Electron app, over both passes; the renderer itself paints a little later in the native app (10.5 to 16.7 ms after navigation start with an empty cache, against 5.6 to 12.2 ms). (A paint entry's start time is its `presentationTime` in both engines, as checked on a page in each, so the paints compare like for like.) In every run, the native app loaded `smoke.html` from the cache, the first page after the empty-cache pass, with its load event at 5 ms, where the Electron app took 50. The earlier pass showed the same.

On the real sites, the native app's runs are the steadier ones: its range never passed 120 ms on an empty cache, where the Electron app's reached 1,280 ms (Wikipedia's load event). Its largest contentful paints are the same as the Electron app's, within 4%: 2 to 4% later in this pass, but which app is ahead does not hold from one pass to the next. An earlier pass of the same scenario, an hour before, had the native app ahead on Wikipedia's largest contentful paint (756 against 848 ms) where this one has it behind (744 against 716). From the renderer's paint to the screen takes 19 to 65 ms in the native app and 19 to 48 ms in the Electron app on these sites.

## Speedometer and JetStream

Speedometer 3.1 and JetStream 3.0, from browserbench.org, run in the app's first tab, in its window, on screen. Each is started once loaded, the way its Start button starts it (`benchmarkClient.start()`, `JetStream.start()`), and read once done: Speedometer's score and its ± over 10 iterations, and JetStream's overall score, the geometric mean of its 77 tests. There were 2 runs per app, each a launch on new data, the apps taking turns, with the throttling switches above. The tab's viewport is each app's default window: 1244×890 in the native app, 1052×741 in the Electron app, both above Speedometer's 850×650 minimum.

| | Native 0.11.2 | Electron 0.6.0 | Change |
| --- | ---: | ---: | --- |
| JetStream 3.0 | 430.82, 439.28 | 399.25, 395.14 | 10% higher |
| Speedometer 3.1 | 46.0 ± 4.1, 46.1 ± 4.3 | 51.4 ± 4.9, 50.9 ± 4.5 | 10% lower |
| Its iterations after the first | 46.9 to 48.7 | 48.0 to 56.1 | |
| Time to run | JetStream 51 s, Speedometer 40 to 45 s | JetStream 54 to 63 s, Speedometer 40 to 45 s | |

JetStream, JavaScript and WebAssembly, scores 10% higher on the native app, in both runs. Speedometer, which drives web apps through the DOM and layout, scores 10% lower on it: lower in each of the ten iterations, in both runs. The first is both apps' lowest (29.8 and 29.1 against 32.9 and 33.5), the second the closest (47.4 and 46.9 against 48.0 and 48.6), and the rest run 47.0 to 48.7 against 52.3 to 56.1. The apps run different Chromium and V8 versions, built differently, in different viewports. These numbers do not say how much of either change is the shell's.

## Caveats

- **Two engines.** Chromium 157 against 146, V8 15.7 against 14.6, and two builds: Pane's `engine/args/release.gn`, which sets no `is_official_build`, and Electron's release build. Wherever the work is the engine's, a change can be the engine's as much as the shell's: JetStream, Speedometer, a page's own loading, a renderer's memory. Telling the two apart would take the same benchmarks on a stock Chromium 157 built from `release.gn` and on Electron 41.4.0 alone, which were not run here; the `Chromium.app` in the local engine checkout is a 156 build from an earlier sync.
- **Two architectures.** The native app's UI is AppKit and SwiftUI, in its browser process. The Electron app's is two web pages in renderers of their own: its UI, and the overlay its tab switcher draws in. Those are the Electron app's two extra renderers, and its window waits for the UI page to load. What each costs is in the launch and memory sections, and comes from the shells as much as the engines.
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
