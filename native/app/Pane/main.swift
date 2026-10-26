import AppKit
import PaneCore

// Chromium owns the process from here: `run` starts the engine on the main
// thread and returns only at shutdown. Nothing may touch NSApplication before
// it, because the engine creates the shared application itself.
let dataDirectory = ProcessInfo.processInfo.environment["PANE_DATA_DIR"].map { URL(fileURLWithPath: $0, isDirectory: true) }
	?? FileManager.default
	.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
	.appendingPathComponent("com.pane.desktop", isDirectory: true)

// Pane restores its own session (profiles.json), so AppKit's window
// restoration stays off, along with its "reopen windows?" alert after a crash.
UserDefaults.standard.register(defaults: [
	"ApplePersistenceIgnoreState": true,
	"NSQuitAlwaysKeepsWindows": false,
])

let appDelegate = AppDelegate(dataDirectory: dataDirectory)

exit(BrowserApplication.run(delegate: appDelegate, dataDirectory: dataDirectory))
