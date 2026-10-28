# Saves run off the main thread and write only what changed

The app saves its state off the main thread, and writes only the file whose state changed, so a page's title, URL, or favicon changing no longer costs Chromium's UI thread an encode and a write. Every such change used to encode every profile, tab, and favicon, then write `profiles.json` and `settings.json`, on the main thread (Chromium's UI thread), 300 ms later. The review's safety track found it (safety P1, and arch P2).

The main thread now only copies the state, which is value types, and a serial utility queue encodes and writes it. Tabs never rewrite `settings.json`, and a first launch that changes no setting writes none. `saveNow` waits for the queue: at quit, and for scenarios that read the files. A failed write marks its file changed again, so the next save retries. A favicon is stored at most 32 pixels a side, the 16 points it draws at, at 2×. The model and its debounced save have since moved into PaneKit, and the reads and writes into the app's `ModelStore`, still on the utility queue (ADR 0077).

Measured in PaneKit with 300 tabs in 6 profiles, a save held the main thread 21.7 ms with 360-pixel favicons (47 KB of base64 each, a 14 MB file). The copy that replaces it takes 0.006 ms. The same tabs with 32-pixel favicons write 1.4 MB in 2.5 ms, off the main thread. `restore` checks that saving tabs leaves `settings.json` unwritten; before, the first save wrote it.

A file that does not decode is kept aside (`profiles.json.damaged`, `settings.json.damaged`) before the first save writes over it, as `security.json` is (ADR 0042). The proxy passwords are written on the same queue, before the file (ADR 0069).
