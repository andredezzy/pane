# Pane locks while open: on command, when the Mac sleeps or locks, and after a time without use

> **On the Views line** (`docs/plans/views-migration.md`, Phase 2): a Lock saves what changed, unloads the pages, empties the model, calls the vault's Lock, closes every engine profile, and detaches once every one has closed; after the detach every process drops its engine keys. A profile still opening answers only the first Lock that came, so a second Lock never leaves the volume attached.

ADR 0113 put every profile's data in an encrypted volume the PIN opens, and Pane locked only at launch: once open, the data stayed readable until Pane quit, by anyone at the Mac and by any process of the user's. The user's requirement (2026-10-31) is that none of Pane's content can be seen without the PIN, and their call, of the options drawn in Paper (06.4, 07.9), was the command, the Mac's own sleep and lock, and an idle time.

What changes:

- **Lock Pane** (⌃⌘L, the Pane menu, after Settings) locks at once; without a PIN it is greyed out.
- **Pane locks by itself** (`LockController`) when the Mac sleeps, its screens sleep or lock (`com.apple.screenIsLocked`), or another user takes the session; and after **Lock after** (Settings, Security: 1 min, 5 min, 15 min, 1 hour, Never; 15 min by default) without use. Pane is in use while it is in front and the Mac has input, which then is Pane's (`CGEventSource`'s time since the last event), and while a page plays sound or shows in picture-in-picture; behind other apps, its clock runs from when it left the front. No key or mouse monitor of its own (ADR 0079).
- **The PIN screen shows in the same frame.** Behind it, every page closes, what changed is written, and the profiles leave the model. The data key leaves memory. Each profile then closes in the engine (`closeContext`): its downloads under way stop as at a quit, the context's hold goes, every page closes without beforeunload, and the profile's Browsers close, which Pane's patch otherwise keeps open without tabs. Chrome then destroys the profile (`DestroyProfileOnBrowserClose`), which closes its databases. Only then does the volume detach.
- **The PIN opens it all again**: the volume attaches, the profiles load, and the tab that showed comes back, still signed in.
- **Only the PIN screen animates** (`RootView`): its slide had an animation over the whole window, which also animated what changed behind it in the same update: the toolbar going and coming with the profiles. Behind the rising screen the window is already empty, and behind the falling one the toolbar is already in place.
- **What was under way waits for the lock.** A context still opening as Pane locks closes once it has opened; a profile being deleted is waited for, and its folder goes at the next open. An attach or a move from an earlier Pane still running is waited for before the detach. The PIN typed meanwhile waits for the detach before attaching again. None of it blocks the main thread.
- **A profile still open 10 s after the lock makes Pane quit**, which closes every profile, and the volume detaches after. The detach is never forced while the engine runs. Until 0.18.2 it was, after 1.55 s, and the keys went whether or not it worked: a profile's `closed` fires as its destruction begins, before its stores close, so a late flush met a volume that was gone. It now tries for 10 s; past that, the keys stay and Pane quits, and the forced detach waits until the engine has stopped. `lock-while-open` holds a file open through a lock, then past the wait.

What was rejected:

- **Forcing the detach after a short wait**, as the first build did: Pane's hosted Browser kept the profile, so its databases stayed open 12 s, and the forced detach pulled them from under the engine. The browser and the network service died of `SIGBUS` on their memory-mapped SQLite files once the PIN opened the data again.
- **Leaving the volume mounted behind the PIN screen**: any process of the user's could read it, which is what the requirement rules out.
- **Locking only the window and keeping the engine's profiles**: their pages, cookies, and caches stay in memory and on a mounted volume.
- **Quitting Pane at every lock**: the window, its tabs, and the place in them would go, where the PIN can bring them back.
- **The window behind the PIN screen animating with it**: the page that opened while the card grew back drew at a size from the way, 757 points tall in a 741-point card, its top under the toolbar.
- **Letting a download finish first**: the data would stay open for as long as the download takes.

How it is checked:

- **`lock-while-open`**: with a PIN, a signed-in tab, and a 4 MB download under way, Lock Pane's reply already shows the PIN screen. The profiles leave the model, the download is cancelled, and the volume is unmounted and detached 219–322 ms after the command over four runs (the detach itself takes 0.16 s, unforced). The PIN brings back the same tab, its cookie, and its local storage, the volume attaching in 0.59 s. The page then fills its card at its own size (`pageLayout`: the size the engine drew it at against its view's); before the fix, 757 against 741. With Lock after at a minute, and time running 60 times faster (`PANE_TIME_SCALE`), Pane locks by itself while it is behind.
- **Once, on this Mac (2026-10-31)**: `lsof` on the mount point during the lock showed the profile's 113 files open for 12 s before the fix and none 250 ms after the command with it. Chrome's `profile_manager` log showed the last `kBrowserWindow` hold going.
