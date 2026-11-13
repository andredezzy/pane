# Pane restores its own session: the app saves it when the engine shuts down, and AppKit's window restoration is off

Session restore is the app's, as it was the Electron app's: Pane persists its own `profiles.json`, and brings its profiles and tabs back from it. It saves as quitting starts, in the callback Chrome runs then (`browser_shutdown::AddAppTerminatingCallback`, from `UiApp::Start`), rather than at `NSApplicationWillTerminateNotification`, which Chromium's quit path does not reliably post. There the model writes what changed (`SaveNow`, off the UI thread, ADR 0068), and the write is waited for before the engine stops; a staged update swaps in after it (ADR 0092).

The session saved is the one the user left, because a closing window or a quitting engine does not report its tabs as removed. `WindowHost` reports no tab removed while a tab strip closes all its tabs, the window closes, or the engine quits; before, the app saw every tab leave at ⌘Q and saved empty profiles. A removed tab means "this tab closed", by the app once beforeunload agreed or by the page itself, and the tabs of a closing window or a quitting engine stay the app's.

AppKit's own window restoration is off, in the defaults Pane registers (`ApplePersistenceIgnoreState`, `pane/app/app_defaults_mac.mm`) and on the window itself (`restorable`, `pane/ui/window/frame_mac.mm`), since Pane restores its session itself, and after a crash AppKit's "reopen windows?" alert blocked the app.

`restore` checks that profiles and their tabs, in order, survive quitting and relaunching.

Until 0.20.0 the Swift app saved at the engine's `browserApplicationWillShutDown:`, which the engine called as quitting started, or after the message loop at the latest.
