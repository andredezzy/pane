# Favicons decode once, off the main thread, into a bounded cache

A tab's row draws the image Chrome's favicon driver sends for its page as it comes, so nothing decodes what the page sent. Only a favicon saved with its tab, a PNG data URL in `profiles.json` (ADR 0068), is decoded: once, off the UI thread, at most 32 pixels a side, into a cache by tab id that keeps 512 tabs (`Favicons`, `pane/ui/tabs/favicons.cc`, with tests). So drawing the sidebar decodes nothing on the UI thread, and the cache cannot grow without bound.

A changing favicon keeps the last one until the next is ready. The saved favicons decode as the profiles load, before a profile expands to show them, and the cache empties at a lock (ADR 0116). The 32 pixels are also the size a favicon is stored at, the 16 points it draws at, at 2× (ADR 0068).

The cache the Swift app first had was a dictionary keyed by whole data URLs, decoded lazily at first draw on the main thread, and never emptied: a tab that changed its favicon 200 times left 202 images. Its replacement, ImageIO into an evictable `NSCache` on a background task, set the rules above, which the Views app keeps.
