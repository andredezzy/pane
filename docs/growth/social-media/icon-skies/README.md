# Icon skies

A 10-second loop for X that shows Pane's ten app-icon skies. A Liquid Glass dock sits on macOS 27's own wallpaper, Golden Gate · Sunset, with Finder, Pane, and Paper; Pane's icon swaps skies faster and faster, with a click on each swap, until it lands on Day with a boom.

`pane-icon-skies-4k.mp4` is the cut the user approved on 2026-10-29: 3840×2160, 60 fps, H.264 with AAC sound, 43 MB. It shows the cartoon skies of Release 0.11.1.

## Rendering

```bash
docs/growth/social-media/icon-skies/source/render.sh
```

The source lives in `source/`. It writes `source/out/pane-icon-skies-4k.mp4` and `source/out/pane-icon-skies-1080p.mp4` in about 40 seconds. It reads the icons from `app/Pane/Assets.xcassets/AppIcon<Sky>`, so after the skies change, running it again makes the video with the new ones. It needs ffmpeg (`brew install ffmpeg`), Xcode's Swift, and Paper.app, whose icon sits in the dock. `source/build/` and `source/out/` stay out of git.

- `rhythm.swift`: which sky each of the 600 frames shows, and the sound. 100 swaps; the first lasts 25 frames and each next one 7% less, down to 3 frames (20 swaps a second); the last lands on Day at 9.2 s. A swap is a knock that rises from 140 to 340 Hz as the swaps speed up, over a 2.4 kHz click; the landing is a boom falling from 90 to 45 Hz under a burst of air. All synthesized, no samples.
- `app-icons.swift`: Finder's and Paper's icons as macOS draws them, at 1024 pixels.
- `compositor.swift`: every frame. It dims the aerial to 0.8, then draws the dock's glass: the backdrop blurred (three 81-pixel box passes), saturated 1.45×, washed 20% white, pushed outward up to 46 pixels in the 70 pixels by the rim (Apple's lens, which the Paper file "Liquid Glass" notes Paper cannot draw), with a specular rim brightest at the top left and bottom right, a glow under the top edge, and a soft shadow. Above it go the three icons at 320 pixels of body, their running dots, "Pane" in Funnel Display Medium tracked −3% (ADR 0099), and "You choose" in SF Mono.
- `render.sh`: ffmpeg decodes the aerial from 0:05 in 4× slow motion (one 240 fps frame per 60 fps frame) and pipes raw frames through the compositor into the encoder.

## Why it looks the way it does

Each point is the user's call, made while the video was drawn:

- **Raw cuts.** The skies swap with no fade or motion, and accelerate into a landing, for drama. A fade between skies was rejected.
- **A sound on every swap,** like a haptic tap, and a heavier one on the landing.
- **Sharp at 4K.** The icons are drawn from their 1024-pixel assets, never enlarged; an icon filling most of the frame read as too big.
- **The dock, after David Hill's opencode dock video** (x.com/iamdavidhill/status/2115889364035366999): the same proportions, doubled, and the background of the newest macOS rather than his Hawaii aerial. Finder on the left rather than Safari; Paper on the right.
- **"Pane" alone, in the wordmark's type,** top left, and "You choose" top right. A pill naming the sky under the dock was removed.
- **The background cuts on the landing frame** instead of cross-fading at the end of the loop: the camera moves, so a cross-fade doubled the rocks. The last frame runs on into the first, so the loop has no seam.

## Earlier cuts

`source/out/earlier/` keeps, outside git, the versions that led here: the icon alone on black (1), the dock on the Hawaii aerial with Safari (2), and the dock on Golden Gate with the realistic skies (3).
