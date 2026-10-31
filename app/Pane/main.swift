import AppKit
import PaneCore
import PaneKit
import os

// Chromium owns the process from here: `run` starts the engine on the main
// thread and returns only at shutdown. Nothing may touch NSApplication before
// it, because the engine creates the shared application itself.
let applicationSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
let dataDirectory = ProcessInfo.processInfo.environment["PANE_DATA_DIR"].map { URL(fileURLWithPath: $0, isDirectory: true) }
	?? applicationSupport.appendingPathComponent(BundleIdentifier.pane, isDirectory: true)

// An earlier Pane's data, under its former identifier's folders, moves to this
// one's before anything reads it (ADR 0114). Scenarios name their own data, or
// a home of their own (CFFIXED_USER_HOME), whose preferences are still the
// user's: those stay.
if ProcessInfo.processInfo.environment["PANE_DATA_DIR"] == nil {
	for failure in BundleIdentifier.moveFormerData(
		applicationSupport: applicationSupport,
		caches: FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0],
		defaults: ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] == nil ? .standard : nil
	) {
		Log.app.error("Could not move an earlier Pane's data: \(failure, privacy: .private)")
	}
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

// What an earlier engine kept in the clear, which it keeps off now (ADR
// 0113), goes with the erase below.
for failure in PanicWipe.stageEngineTraces(dataDirectory: dataDirectory) {
	Log.security.error("Could not stage an earlier engine's traces: \(failure.description, privacy: .private)")
}

// The panic wipe's data goes before anything reads the data directory: the
// wiped instance started this one, or a wipe was cut short (AppLock).
AppLock.wipeAtLaunch(dataDirectory)

// The first launch after the Electron app brings its data over, from a copy
// (LaunchMigration), before anything here reads the data directory.
let electron = ProcessInfo.processInfo.environment["PANE_ELECTRON_DATA_DIR"].map { ElectronMigration.Source(directory: URL(fileURLWithPath: $0, isDirectory: true)) }
	?? ElectronMigration.defaultSource(applicationSupport: FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0])

LaunchMigration.run(from: electron, into: dataDirectory)

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
