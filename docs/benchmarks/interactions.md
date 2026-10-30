# How much sooner Pane answers a tab switch, a new tab, and the address field

What 0.13.1 changed in the time from an input to the frame that answers it: selecting a tab, a new tab, and focusing the address field, measured on one Mac against the code before the change, with the same marks and the same scenario for both. [interactions.json](interactions.json) holds every run. Numbers are the median of six runs' medians, with the runs' range in brackets; p90 is the median of the runs' 90th percentiles.

## What improved

| | 0.13.1 | Before | Improvement |
| --- | ---: | ---: | --- |
| Selecting a tab | 15.8 ms [13.7–18.3] | 25.5 ms [20.7–31.6] | 9.7 ms sooner (−38%) |
| A new tab | 19.1 ms [12.1–22.2] | 34.5 ms [31.3–38.3] | 15.4 ms sooner (−45%) |
| Focusing the address field | 7.5 ms [7.2–11.2] | 16.5 ms [14.3–17.4] | 9.0 ms sooner (−55%) |
| Selecting a tab, p90 | 21.5 ms | 35.5 ms | 14.0 ms sooner |
| A new tab, p90 | 23.1 ms | 37.9 ms | 14.8 ms sooner |
| Focusing the address field, p90 | 15.2 ms | 19.7 ms | 4.5 ms sooner |

Every one of the six runs of 0.13.1 was faster than every run before it, for each of the three: the ranges do not overlap.

- **Selecting a tab** hid the tab left first: its view hidden and Chromium's `WasHidden`, 4.5 of `select(tabID:)`'s 6.5 ms, all before the frame. The window removes that view as it shows the new one, so 0.13.1 hides it after the frame (`hideAfterFrame`).
- **A new tab** waited for the engine to make its page, 11.4 ms, inside the input's turn. 0.13.1 makes it on the turn after the frame: the frame that answers shows the new row selected and the address field focused, and the page follows a frame later. A tab selected only in passing, as one cycles through them, is no longer made at all. URLs another app hands over still load each at once.
- **Focusing the address field** is sooner too, from the menu and in a new tab alike. Its steps were not timed apart, so which of the two changes brings it is not shown here.

## What did not improve enough

- **Selecting a tab and a new tab still take more than a frame** at 120 Hz (8.3 ms) as measured here. Selecting a tab takes less than one at 60 Hz (16.7 ms) at the median; a new tab, 19.1 ms, a little more. The intervals also hold the automation channel's own reply and whatever else the run loop runs in the input's turn; [Where a tab switch's time goes](#where-a-tab-switchs-time-goes) counts the work itself.
- **The p90 of focusing the address field**, 15.2 ms, is twice its median: the focuses that come with a new tab run in the new tab's turn.

## Where a tab switch's time goes

`sample` (1 ms) on 0.13.1's main thread while the automation channel switched among five loaded tabs, 120 switches in 12 s: 10.6 ms of work a switch, 9.3 ms without the channel's own (reading the command, the state it replies with). About 0.9 ms of it, hiding the tab left, runs after the frame, so a switch's work ahead of its frame is about 8.4 ms, one frame at 120 Hz. In it:

| | Per switch |
| --- | ---: |
| The frame's commit (Core Animation, AppKit's display cycle) | 4.2 ms |
| ↳ SwiftUI's layout of the window (`NSHostingView.layout`) | 2.4 ms |
| ↳ the window's drag regions (`_resetDragMargins`) | 0.8 ms |
| ↳ the commit to the window server | 0.7 ms |
| `select(tabID:)` | 2.5 ms |
| ↳ the page's view out of the window and the next one in | 1.1 ms |
| ↳ the page's focus (SwiftUI's first responder observer) | 0.7 ms |
| SwiftUI's own run loop update | 1.6 ms |

Pane's own view bodies are under 0.2 ms of it: what is left is SwiftUI's and AppKit's work for the window's views. Keeping loaded tabs' views in the window, hidden, rather than swapping them was tried: `select(tabID:)` fell by 1 ms, and the layout and the commit grew by about as much, with every page's view to lay out, so the frame was no sooner (and hidden pages in the window would resize with it). Getting further means fewer views for the window to lay out each frame, as Dia drew its own on AppKit alone.

## Method

`tests/scenarios/interaction-timing.ts` launches the build with five loaded tabs and repeats each interaction through the automation channel: 77 tab selections a run (40 among the five, 16 back from a new tab, the 16 each new tab makes, 5 as the tabs open), 16 new tabs from File › New Tab, and 32 address field focuses (the new tabs' 16, and 16 from Navigate › Focus Address Bar). It reads the intervals from the unified log as they happen (`/usr/bin/log stream --signpost`; zsh has a builtin named `log`).

Each interval is the app's own (`Log.interaction`): it begins as the input is handled and ends once Core Animation has committed the frame built in its turn to the window server, in a run loop observer just after Core Animation's (`AfterFrame`). It leaves out what follows the commit: the window server compositing that frame for the display's next refresh, and a page's own content, which its renderer and the GPU process draw.

The two builds alternated, the order flipped every round, so a trend in the Mac's load fell on both. The Mac was in use (other apps busy, load averages 3.6 to 4.8): the absolute numbers carry its load, and the comparison between the two holds within it.

| | |
| --- | --- |
| Mac | MacBook Pro (Mac17,9), Apple M5 Pro, 18 cores, 48 GB |
| Display | built-in 14-inch ProMotion, 3024 × 1964, up to 120 Hz |
| macOS | 27.0.1 (26A434) |
| Engine | Chromium 157.0.8103.26, engine-95313854dfb3, in both |
| 0.13.1 | the app out of its release dmg |
| Before | the app with 3d85ef7's `BrowserController.swift` (before both changes) and f8a07e6's marks (`Log.interaction`, `AfterFrame`): 0.13.0 has no marks to read |

## Rerunning

```bash
# 0.13.1, out of its dmg.
mkdir -p /tmp/bench-int/{dl,mnt,after}
gh release download v0.13.1 -R andredezzy/pane -p 'Pane-0.13.1-arm64.dmg' -D /tmp/bench-int/dl
diskutil image attach --readOnly --nobrowse --mountPoint /tmp/bench-int/mnt /tmp/bench-int/dl/Pane-0.13.1-arm64.dmg
ditto /tmp/bench-int/mnt/Pane.app /tmp/bench-int/after/Pane.app
diskutil eject /tmp/bench-int/mnt

# The code before, with the marks: built beside the checkout's own app.
git show 3d85ef7:app/Pane/Browser/BrowserController.swift > app/Pane/Browser/BrowserController.swift
PANE_APP_OUT=/tmp/bench-int/before/Pane.app engine/scripts/package.sh
git checkout -- app/Pane/Browser/BrowserController.swift

# Each run takes about 25 seconds; alternate the two.
bun tests/run.ts --binary /tmp/bench-int/after/Pane.app/Contents/MacOS/Pane --out /tmp/bench-int/after-1 interaction-timing
bun tests/run.ts --binary /tmp/bench-int/before/Pane.app/Contents/MacOS/Pane --out /tmp/bench-int/before-1 interaction-timing
```

Each run's `interaction-timing.json` holds every interaction's count, median, p90, and range. The scenario never takes the front from the app in use.
