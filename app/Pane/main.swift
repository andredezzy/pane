import AppKit
import PaneCore
import PaneKit
import os

// Chromium owns the process from here: `run` starts the engine on the main
// thread and returns only at shutdown. Nothing may touch NSApplication before
// it, because the engine creates the shared application itself.
let dataDirectory = TestHooks.environment("PANE_DATA_DIR").map { URL(fileURLWithPath: $0, isDirectory: true) }
	?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent(BundleIdentifier.pane, isDirectory: true)

// The data and its caches are the user's alone: other accounts on this Mac
// read them at 0755.
for directory in [dataDirectory, AppLock.cacheDirectory(for: dataDirectory)].compactMap(\.self) {
	try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
	chmod(directory.path, 0o700)
}

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
// wiped instance started this one, or a wipe was cut short (PanicWipeRun).
PanicWipeRun.atLaunch(dataDirectory)

// Chromium opens the profile it used last at startup, before the PIN: never
// one of Pane's, whose data is in the volume the data key opens (ADR 0113).
do {
	try LocalState.startOnDefaultProfile(userDataDirectory: dataDirectory.appending(path: "Engine"))
} catch {
	Log.engine.error("Could not point Local State at the Default profile: \(String(describing: error), privacy: .private)")
}

// The Pane that runs on this data holds its lock until it ends. One started
// while it runs, which hands its URLs over through the engine's singleton and
// quits, or while it quits, leaves its image alone (DataLock, ADR 0113).
let dataLock = DataLock(dataDirectory: dataDirectory)

// The data image attaches now, its volume locked, while the engine starts:
// only the unlock and the mount wait for the key (ADR 0113).
let image = dataLock.map { _ in
	DataVault.attachImage(DataVolume(dataDirectory: dataDirectory, cacheDirectory: AppLock.cacheDirectory(for: dataDirectory)))
}

let appDelegate = AppDelegate(dataDirectory: dataDirectory, dataLock: dataLock, image: image)
let status = BrowserApplication.run(delegate: appDelegate, dataDirectory: dataDirectory)

// The engine is down, its files closed: the data volume detaches (ADR 0113).
appDelegate.engineDidStop()
exit(status)
