import AppKit
import PaneCore
import PaneKit

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
	// The app's text as the Electron app draws its UI (CSS
	// -webkit-font-smoothing: antialiased): no font smoothing.
	"AppleFontSmoothing": 0,
	"NSQuitAlwaysKeepsWindows": false,
])

// The panic wipe's data goes before anything reads the data directory: the
// wiped instance started this one, or a wipe was cut short (AppLock).
AppLock.wipeAtLaunch(dataDirectory)

// The first launch after the Electron app brings its data over, from a copy
// (LaunchMigration), before anything here reads the data directory.
let electron = ProcessInfo.processInfo.environment["PANE_ELECTRON_DATA_DIR"].map { ElectronMigration.Source(directory: URL(fileURLWithPath: $0, isDirectory: true)) }
	?? ElectronMigration.defaultSource(applicationSupport: FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0])

LaunchMigration.run(from: electron, into: dataDirectory)

// Profiles deleted whose engine data the engine never confirmed gone: their
// folders go now, while nothing has them open (ProfileDeletions).
for failure in ProfileDeletions(dataDirectory: dataDirectory).purge(dataDirectory: dataDirectory, cacheDirectory: AppLock.cacheDirectory(for: dataDirectory)) {
	NSLog("Pane: could not remove a deleted profile's data: %@", failure)
}

let appDelegate = AppDelegate(dataDirectory: dataDirectory)

exit(BrowserApplication.run(delegate: appDelegate, dataDirectory: dataDirectory))
