# Favicons decode once, off the main thread, into a bounded cache

ImageIO decodes each tab's favicon once, on a background task, at most 32 pixels a side, into an `NSCache` by tab id that keeps 512 tabs and is evictable (`Favicons`, in `app/Pane/Views/Components/Favicon.swift`), so drawing the sidebar decodes nothing on the main thread and the cache cannot grow without bound. The cache it replaced was a dictionary keyed by whole data URLs, decoded lazily at first draw on the main thread, and never emptied: a tab that changed its favicon 200 times left 202 images.

A row compares its tab's data URL with the one decoded, the same string, so the check is at once. A changing favicon keeps the last one until the next is ready. The saved favicons decode as the window opens, before a profile expands to show them. The 32 pixels are also the size a favicon is stored at, the 16 points it draws at, at 2× (ADR 0068).

The sidebar renders as before. At these sizes neither build, the old or the new, spends a sampled millisecond of the main thread decoding.
