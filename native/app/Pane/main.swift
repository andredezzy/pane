import AppKit
import PaneCore

// Chromium owns the process from here: `run` starts the engine on the main
// thread and returns only at shutdown. Nothing may touch NSApplication before
// it, because the engine creates the shared application itself.
let dataDirectory = FileManager.default
	.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
	.appendingPathComponent("com.pane.desktop", isDirectory: true)

let appDelegate = AppDelegate()

exit(BrowserApplication.run(delegate: appDelegate, dataDirectory: dataDirectory))
